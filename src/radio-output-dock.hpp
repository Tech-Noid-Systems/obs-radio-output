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

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

/*
 * Persistent OBS dock widget with connection status and Start / Stop
 * buttons.  Registered by frontend-wire.cpp via
 * obs_frontend_add_dock_by_id().
 *
 * Lifetime: owned by OBS after registration (for the process lifetime).
 * State polled every 500 ms via QTimer from the main thread.
 */
class RadioOutputDock : public QFrame {
	Q_OBJECT

public:
	explicit RadioOutputDock(QWidget *parent = nullptr);

private slots:
	void onStart();
	void onStop();
	void pollState();

private: // NOLINT(readability-redundant-access-specifiers) — visually separate Qt slots from members
	QLabel *status_;
	QPushButton *start_;
	QPushButton *stop_;
	QTimer *poll_;
};
