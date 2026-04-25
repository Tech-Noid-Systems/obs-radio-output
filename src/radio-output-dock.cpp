// SPDX-License-Identifier: GPL-2.0-or-later
/*
obs-radio-output
Copyright (C) 2026 Aaron Cupp <mrcupp@mrcupp.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "radio-output-dock.hpp"

#include <QByteArray>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QString>
#include <QThreadPool>
#include <QVBoxLayout>

#include <obs-module.h>
#include <pthread.h>

#include "frontend-wire.h"
#include "metadata.h"
#include "radio-output.h"

namespace {

/* Poll interval for the connection-state label.  500 ms keeps the UI
 * responsive without spinning the CPU; it's also short enough that state
 * transitions feel instant to the user. */
constexpr int kPollIntervalMs = 500;

struct StateDisplay {
	const char *locale_key;
	const char *stylesheet_color;
};

StateDisplay displayFor(radio_state_t state)
{
	switch (state) {
	case RADIO_STATE_CONNECTING:
		return {"RadioOutput.State.Connecting", "#d6a100"}; /* amber */
	case RADIO_STATE_CONNECTED:
		return {"RadioOutput.State.Connected", "#2ca14b"}; /* green */
	case RADIO_STATE_RECONNECTING:
		return {"RadioOutput.State.Reconnecting", "#d17d00"}; /* orange */
	case RADIO_STATE_ERROR:
		return {"RadioOutput.State.Error", "#c0392b"}; /* red */
	case RADIO_STATE_DISCONNECTED:
	default:
		return {"RadioOutput.State.Disconnected", "#888888"}; /* grey */
	}
}

} // namespace

RadioOutputDock::RadioOutputDock(QWidget *parent) : QFrame(parent)
{
	setFrameShape(QFrame::NoFrame);

	status_ = new QLabel(obs_module_text("RadioOutput.State.Disconnected"), this);
	QFont font = status_->font();
	font.setPointSize(font.pointSize() + 2);
	font.setBold(true);
	status_->setFont(font);
	status_->setAlignment(Qt::AlignCenter);

	start_ = new QPushButton(obs_module_text("RadioOutput.Dock.Start"), this);
	stop_ = new QPushButton(obs_module_text("RadioOutput.Dock.Stop"), this);
	stop_->setEnabled(false);

	connect(start_, &QPushButton::clicked, this, &RadioOutputDock::onStart);
	connect(stop_, &QPushButton::clicked, this, &RadioOutputDock::onStop);

	auto *buttons = new QHBoxLayout;
	buttons->addWidget(start_);
	buttons->addWidget(stop_);

	nowPlayingLabel_ = new QLabel(obs_module_text("RadioOutput.Dock.NowPlaying"), this);
	nowPlayingEdit_ = new QLineEdit(this);
	nowPlayingEdit_->setPlaceholderText(obs_module_text("RadioOutput.Dock.NowPlaying.Placeholder"));
	nowPlayingEdit_->setEnabled(false);
	pushMetadata_ = new QPushButton(obs_module_text("RadioOutput.Dock.NowPlaying.Push"), this);
	pushMetadata_->setEnabled(false);

	connect(pushMetadata_, &QPushButton::clicked, this, &RadioOutputDock::onPushMetadata);
	connect(nowPlayingEdit_, &QLineEdit::returnPressed, this, &RadioOutputDock::onPushMetadata);

	listenerLabel_ = new QLabel(QString::fromUtf8(obs_module_text("RadioOutput.Dock.Listeners"))
					    .arg(obs_module_text("RadioOutput.Dock.Listeners.Unknown")),
				    this);

	listenerPoll_ = new ListenerPoll(this);
	connect(listenerPoll_, &ListenerPoll::listenerCount, this, &RadioOutputDock::onListenerCount);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(status_);
	layout->addLayout(buttons);
	layout->addWidget(nowPlayingLabel_);
	layout->addWidget(nowPlayingEdit_);
	layout->addWidget(pushMetadata_);
	layout->addWidget(listenerLabel_);
	layout->addStretch(1);

	poll_ = new QTimer(this);
	poll_->setInterval(kPollIntervalMs);
	connect(poll_, &QTimer::timeout, this, &RadioOutputDock::pollState);
	poll_->start();
}

void RadioOutputDock::onStart()
{
	frontend_wire_start_output();
	/* State update happens via pollState() on next tick, so the button
	 * enabled-state flips then rather than optimistically here. */
}

void RadioOutputDock::onStop()
{
	frontend_wire_stop_output();
}

void RadioOutputDock::onPushMetadata()
{
	if (metadataInFlight_)
		return;

	const QString text = nowPlayingEdit_->text().trimmed();
	if (text.isEmpty())
		return;

	/* Cap at 255 UTF-8 bytes to match the C-side truncation so the
	 * log line here and the on-wire payload agree. */
	const QByteArray utf8 = text.toUtf8().left(255);

	metadataInFlight_ = true;
	pushMetadata_->setEnabled(false);

	/* QThreadPool ships in Qt6::Core — QtConcurrent does not ship with
	 * OBS's bundled Qt frameworks on macOS, so depending on it would
	 * break plugin loading on every end-user install.  Dispatching via
	 * the global pool + QMetaObject::invokeMethod(QueuedConnection) gets
	 * us the same "run off-main-thread, reply on main" flow with zero
	 * extra framework dependencies.  invokeMethod safely drops the
	 * reply if `this` has been destroyed before the pool job returns. */
	QThreadPool::globalInstance()->start([this, utf8] {
		radio_output_update_metadata(utf8.constData());
		QMetaObject::invokeMethod(
			this,
			[this] {
				metadataInFlight_ = false;
				/* Re-run pollState() so the user doesn't wait up
				 * to 500 ms for the button to re-enable. */
				pollState();
			},
			Qt::QueuedConnection);
	});
}

void RadioOutputDock::onListenerCount(int count)
{
	QString countText;
	if (count < 0)
		countText = obs_module_text("RadioOutput.Dock.Listeners.Unknown");
	else
		countText = QString::number(count);

	listenerLabel_->setText(QString::fromUtf8(obs_module_text("RadioOutput.Dock.Listeners")).arg(countText));
	/* Amber on error, default on success.  Same vocabulary the status row
	 * uses for its RECONNECTING amber. */
	listenerLabel_->setStyleSheet(count < 0 ? QStringLiteral("QLabel { color: #d17d00; }") : QString());
}

void RadioOutputDock::pollState()
{
	struct radio_output *ctx = radio_output_get_active();
	radio_state_t state = RADIO_STATE_DISCONNECTED;
	QString host;
	int port = 0;
	int protocol = RADIO_PROTOCOL_ICECAST;
	bool use_tls = false;
	QString mount;
	if (ctx) {
		pthread_mutex_lock(&ctx->state_mutex);
		state = ctx->state;
		/* Snapshot the fields we might need for the poller while we
		 * hold the mutex.  host/mount are char* allocated during
		 * radio_output_start() and freed during teardown; copying them
		 * here decouples from that lifetime. */
		if (ctx->host)
			host = QString::fromUtf8(ctx->host);
		port = ctx->port;
		protocol = ctx->protocol;
		use_tls = ctx->use_tls;
		if (ctx->mount)
			mount = QString::fromUtf8(ctx->mount);
		pthread_mutex_unlock(&ctx->state_mutex);
	}

	const StateDisplay disp = displayFor(state);
	status_->setText(obs_module_text(disp.locale_key));
	status_->setStyleSheet(QString("QLabel { color: %1; }").arg(disp.stylesheet_color));

	const bool running = (state == RADIO_STATE_CONNECTING || state == RADIO_STATE_CONNECTED ||
			      state == RADIO_STATE_RECONNECTING);
	/* Error is sticky until the user clicks Stop — disable Start so the
	 * only action available is acknowledging the failure.  Otherwise a
	 * second Start click would silently no-op against the retained
	 * errored handle (see frontend_wire_start_output). */
	start_->setEnabled(!running && state != RADIO_STATE_ERROR);
	stop_->setEnabled(running || state == RADIO_STATE_ERROR);

	const bool connected = (state == RADIO_STATE_CONNECTED);
	nowPlayingEdit_->setEnabled(connected);
	pushMetadata_->setEnabled(connected && !metadataInFlight_);

	/* Listener poller lifecycle — edge-triggered on state transitions
	 * rather than running it every 500 ms.  Enters CONNECTED: configure
	 * + start; leaves CONNECTED: stop and reset label to "—". */
	const bool wasConnected = (lastState_ == RADIO_STATE_CONNECTED);
	if (connected && !wasConnected) {
		const QString scheme = use_tls ? QStringLiteral("https") : QStringLiteral("http");
		listenerPoll_->configure(scheme, host, port, protocol, mount);
		listenerPoll_->start();
	} else if (!connected && wasConnected) {
		listenerPoll_->stop();
		/* Reset to neutral "—" (not the amber error state that
		 * onListenerCount(-1) would paint — we're not in an error,
		 * we just aren't polling). */
		listenerLabel_->setText(QString::fromUtf8(obs_module_text("RadioOutput.Dock.Listeners"))
						.arg(obs_module_text("RadioOutput.Dock.Listeners.Unknown")));
		listenerLabel_->setStyleSheet(QString());
	}
	lastState_ = state;
}
