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

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>

#include <obs.h>

/*
 * Modal configuration dialog for the Radio Output plugin.
 *
 * Reads the current settings from the given obs_data_t on construction.
 * On OK, writes the edited values back into the same obs_data_t and
 * accepts.  On Cancel, the obs_data_t is untouched.  Persistence to disk
 * is the caller's responsibility (see frontend-wire.cpp).
 */
class RadioOutputConfigDialog : public QDialog {
	Q_OBJECT

public:
	explicit RadioOutputConfigDialog(obs_data_t *settings, QWidget *parent = nullptr);

private slots:
	void onAccept();

private:
	obs_data_t *settings_;

	QLineEdit *host_;
	QSpinBox *port_;
	QLineEdit *mount_;
	QLineEdit *password_;
	QComboBox *codec_;
	QComboBox *bitrate_;
	QCheckBox *reconnect_enabled_;
	QSpinBox *reconnect_delay_;
	QSpinBox *reconnect_max_;
	QCheckBox *start_with_streaming_;
};
