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
#include <QFormLayout>
#include <QLabel>
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

private: // NOLINT(readability-redundant-access-specifiers) — keeps data members visually separate from Qt slots
	obs_data_t *settings_;

	/* Server group: the protocol combo drives mount- and TLS-row
	 * visibility via the protocol-changed slot (SHOUTcast v1 has neither
	 * a mount path nor TLS). */
	QComboBox *protocol_;
	QLineEdit *host_;
	QSpinBox *port_;
	QLineEdit *mount_;
	QLineEdit *password_;
	QCheckBox *tls_enabled_;

	QFormLayout *server_form_;
	int mount_row_index_;
	int tls_row_index_;

	QComboBox *codec_;
	QComboBox *bitrate_;

	/* Audio group.  The codec combo drives row visibility: channel mode and
	 * quality are MP3-only; the samplerate note is Opus-only (Opus is always
	 * 48 kHz).  All toggled in onCodecChanged. */
	QComboBox *channel_mode_;
	QComboBox *bitrate_mode_;
	QComboBox *vbr_quality_;
	QComboBox *vbr_min_;
	QComboBox *vbr_max_;
	QComboBox *quality_;
	QComboBox *samplerate_;
	QLabel *samplerate_note_;
	QFormLayout *audio_form_;
	int channel_mode_row_index_;
	int bitrate_mode_row_index_;
	int vbr_quality_row_index_;
	int vbr_min_row_index_;
	int vbr_max_row_index_;
	int quality_row_index_;
	int samplerate_note_row_index_;

	/* Recompute MP3/codec/bitrate-mode-dependent row visibility.  Called from
	 * both onCodecChanged and onBitrateModeChanged. */
	void updateAudioVisibility();

	QCheckBox *reconnect_enabled_;
	QSpinBox *reconnect_delay_;
	QSpinBox *reconnect_max_;
	QCheckBox *start_with_streaming_;

private slots: // NOLINT(readability-redundant-access-specifiers)
	void onProtocolChanged(int index);
	void onCodecChanged(int index);
	void onBitrateModeChanged(int index);
};
