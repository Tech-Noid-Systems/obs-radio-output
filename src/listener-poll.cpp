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

#include "listener-poll.hpp"
#include "radio-output.h"

#include <obs-module.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>

#include <plugin-support.h>

namespace {

/*
 * Parse an Icecast /status-json.xsl body.  The "source" field is an
 * object when the server has exactly one mount and an array of objects
 * when it has multiple.  We must match on our mount (not just grab the
 * first/only entry) so a multi-mount Icecast reports the correct count.
 *
 * Returns -1 on any parse failure or mount mismatch.
 */
int parseIcecastListeners(const QByteArray &body, const QString &mount)
{
	const QJsonDocument doc = QJsonDocument::fromJson(body);
	if (!doc.isObject())
		return -1;

	const QJsonObject stats = doc.object().value("icestats").toObject();
	const QJsonValue sources = stats.value("source");

	auto matchSource = [&mount](const QJsonObject &src) -> int {
		const QString listenurl = src.value("listenurl").toString();
		if (!listenurl.endsWith(mount))
			return -1;
		/* "listeners" is usually a number; some Icecast builds emit it
		 * as a string.  toInt(-1) handles both transparently. */
		return src.value("listeners").toVariant().toInt();
	};

	if (sources.isArray()) {
		/* Bind the array to a named local so clang's
		 * -Wrange-loop-bind-reference doesn't complain about `v`
		 * referencing a temporary.  The temporary would actually
		 * survive the loop per C++17 lifetime rules, but the
		 * warning doesn't know that and we build with -Werror. */
		const QJsonArray arr = sources.toArray();
		for (const QJsonValue &v : arr) {
			const int n = matchSource(v.toObject());
			if (n >= 0)
				return n;
		}
		return -1;
	}
	if (sources.isObject())
		return matchSource(sources.toObject());

	return -1;
}

/*
 * Parse a SHOUTcast v1 /7.html body — an HTML wrapper around a comma-
 * separated stats line:
 *
 *   <HTML><body>3,1,10,100,2,128,Now Playing</body></HTML>
 *
 * Fields: currentlisteners, streamstatus, peaklisteners, maxlisteners,
 * uniquelisteners, bitrate, songtitle.  We want the first one.  Some
 * DNAS builds omit the HTML wrapper; handle both.
 *
 * Returns -1 on any parse failure.
 */
int parseShoutcastListeners(const QByteArray &body)
{
	QString s = QString::fromUtf8(body).trimmed();

	const int lt = s.indexOf(QLatin1String("<body>"));
	const int gt = s.indexOf(QLatin1String("</body>"));
	if (lt >= 0 && gt > lt)
		s = s.mid(lt + 6, gt - lt - 6);

	const QStringList fields = s.split(QLatin1Char(','));
	if (fields.isEmpty())
		return -1;

	bool ok = false;
	const int n = fields.first().trimmed().toInt(&ok);
	return ok ? n : -1;
}

} // namespace

ListenerPoll::ListenerPoll(QObject *parent) : QObject(parent)
{
	timer_.setSingleShot(false);
	connect(&timer_, &QTimer::timeout, this, &ListenerPoll::pollOnce);
}

void ListenerPoll::configure(const QString &scheme, const QString &host, int port, int protocol, const QString &mount)
{
	scheme_ = scheme;
	host_ = host;
	port_ = port;
	protocol_ = protocol;
	mount_ = mount;
	had_error_ = false;
}

void ListenerPoll::start(int interval_ms)
{
	if (timer_.isActive() && timer_.interval() == interval_ms)
		return;
	timer_.setInterval(interval_ms);
	timer_.start();
	/* Fire one immediately so the UI doesn't sit at "—" for the first
	 * interval after going Live. */
	pollOnce();
}

void ListenerPoll::stop()
{
	timer_.stop();
	had_error_ = false;
}

void ListenerPoll::pollOnce()
{
	if (host_.isEmpty() || port_ <= 0)
		return;

	QUrl url;
	url.setScheme(scheme_);
	url.setHost(host_);
	url.setPort(port_);
	if (protocol_ == RADIO_PROTOCOL_SHOUTCAST)
		url.setPath(QStringLiteral("/7.html"));
	else
		url.setPath(QStringLiteral("/status-json.xsl"));

	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("obs-radio-output/listener-poll"));
	/* SHOUTcast's /7.html traditionally requires a browser-like UA header
	 * to emit the HTML page (vs. an admin-redirect).  Modern DNAS builds
	 * are lenient, but the UA helps us match older servers too. */
	req.setRawHeader("Icy-MetaData", "0");

	QNetworkReply *reply = nam_.get(req);
	const int captured_protocol = protocol_;
	const QString captured_mount = mount_;
	connect(reply, &QNetworkReply::finished, this, [this, reply, captured_protocol, captured_mount] {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			if (!had_error_) {
				obs_log(LOG_WARNING, "listener-poll: request failed: %s",
					reply->errorString().toUtf8().constData());
				had_error_ = true;
				emit listenerCount(-1);
			}
			return;
		}

		const QByteArray body = reply->readAll();
		int count = -1;
		if (captured_protocol == RADIO_PROTOCOL_SHOUTCAST)
			count = parseShoutcastListeners(body);
		else
			count = parseIcecastListeners(body, captured_mount);

		if (count < 0) {
			if (!had_error_) {
				obs_log(LOG_WARNING, "listener-poll: could not parse listener count for mount '%s'",
					captured_mount.toUtf8().constData());
				had_error_ = true;
				emit listenerCount(-1);
			}
			return;
		}

		had_error_ = false;
		emit listenerCount(count);
	});
}
