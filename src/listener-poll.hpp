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

#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>

/*
 * Periodic listener-count poller.  Hits Icecast's /status-json.xsl or
 * SHOUTcast v1's /7.html on a QTimer and emits listenerCount(int) on
 * each successful parse; emits listenerCount(-1) once per continuous
 * error-run so the UI can fall back to "—".
 *
 * Runs entirely on the Qt main thread — QNetworkAccessManager's async
 * API means no blocking even during HTTP round-trips.  Intended to be
 * owned by the dock widget; configure() is idempotent (safe to call on
 * every reconnect), start() is idempotent (safe to call when already
 * running).
 */
class ListenerPoll : public QObject {
	Q_OBJECT

public:
	explicit ListenerPoll(QObject *parent = nullptr);

	/*
	 * Configure the next poll's target.  Call on DISCONNECTED → CONNECTED
	 * transition with the active radio_output's host/port/protocol/mount.
	 * Values are copied; caller can free after return.
	 *
	 * protocol: RADIO_PROTOCOL_ICECAST or RADIO_PROTOCOL_SHOUTCAST from
	 * radio-output.h.  Mount is only consulted for Icecast (SHOUTcast v1
	 * doesn't have mounts).  Scheme: "http" or "https" based on use_tls.
	 */
	void configure(const QString &scheme, const QString &host, int port, int protocol, const QString &mount);

	void start(int interval_ms = 10000);
	void stop();

signals:
	/*
	 * Emitted on the main thread after each poll.  -1 indicates the poll
	 * failed (HTTP error, parse error, wrong mount, etc.).  Subscribers
	 * can distinguish "never yet polled" (don't fire at all until first
	 * poll completes) from "polled and got -1" (error) by tracking their
	 * own state.
	 */
	void listenerCount(int count);

private slots:
	void pollOnce();

private: // NOLINT(readability-redundant-access-specifiers)
	QNetworkAccessManager nam_;
	QTimer timer_;
	QString scheme_;
	QString host_;
	int port_ = 0;
	int protocol_ = 0; /* RADIO_PROTOCOL_ICECAST / _SHOUTCAST */
	QString mount_;
	bool had_error_ = false; /* suppress repeat -1 emissions during an error run */
	/* Suppress the very first post-configure() poll failure as a likely
	 * server-bootstrap race (e.g. Icecast restart: source reconnects in
	 * ~1-3 s, but /status-json.xsl can take longer to come up).  Without
	 * this, every reconnect would flash amber "—" for one poll interval
	 * before settling.  Counts completed polls, not dispatched ones, so
	 * we don't double-count an in-flight request that hasn't returned. */
	int polls_completed_ = 0;
};
