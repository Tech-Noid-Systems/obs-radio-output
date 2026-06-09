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

#include "radio-output-config-dialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

#include <obs-module.h>

#include "radio-output.h"

namespace {

/* Common internet-radio bitrates populated into the combo. */
constexpr int kBitrates[] = {32, 48, 64, 96, 128, 192, 256, 320};

/* Selectable stream output samplerates (Hz).  "Match OBS" (data 0) is prepended
 * separately.  Only MP3 resamples to these; Opus is fixed at 48 kHz and Vorbis
 * encodes at the OBS input rate (see the per-codec handling in the encoders). */
constexpr int kSamplerates[] = {22050, 32000, 44100, 48000};

QWidget *buildServerGroup(RadioOutputConfigDialog *parent, QComboBox *&protocol, QLineEdit *&host, QSpinBox *&port,
			  QLineEdit *&mount, QLineEdit *&password, QCheckBox *&tls_enabled, QFormLayout *&form_out,
			  int &mount_row_index_out, int &tls_row_index_out)
{
	auto *group = new QGroupBox(obs_module_text("RadioOutput.Server.Group"), parent);
	auto *form = new QFormLayout(group);

	protocol = new QComboBox(group);
	protocol->addItem(obs_module_text("RadioOutput.Server.Protocol.Icecast"), RADIO_PROTOCOL_ICECAST);
	protocol->addItem(obs_module_text("RadioOutput.Server.Protocol.SHOUTcast"), RADIO_PROTOCOL_SHOUTCAST);
	form->addRow(obs_module_text("RadioOutput.Server.Protocol"), protocol);

	host = new QLineEdit(group);
	form->addRow(obs_module_text("RadioOutput.Server.Host"), host);

	port = new QSpinBox(group);
	port->setRange(1, 65535);
	port->setMinimumWidth(100);
	form->addRow(obs_module_text("RadioOutput.Server.Port"), port);

	mount = new QLineEdit(group);
	mount_row_index_out = form->rowCount(); /* remember for setRowVisible() when protocol changes */
	form->addRow(obs_module_text("RadioOutput.Server.Mount"), mount);

	password = new QLineEdit(group);
	password->setEchoMode(QLineEdit::Password);
	form->addRow(obs_module_text("RadioOutput.Server.Password"), password);

	tls_enabled = new QCheckBox(obs_module_text("RadioOutput.Server.UseTLS"), group);
	tls_row_index_out = form->rowCount();
	form->addRow(QString(), tls_enabled);

	form_out = form;
	return group;
}

struct AudioGroupWidgets {
	QComboBox *&codec;
	QComboBox *&channel_mode;
	QComboBox *&bitrate;
	QComboBox *&bitrate_mode;
	QComboBox *&vbr_quality;
	QComboBox *&vbr_min;
	QComboBox *&vbr_max;
	QComboBox *&quality;
	QComboBox *&samplerate;
	QLabel *&samplerate_note;
	QFormLayout *&form_out;
	int &channel_mode_row_index_out;
	int &bitrate_mode_row_index_out;
	int &vbr_quality_row_index_out;
	int &vbr_min_row_index_out;
	int &vbr_max_row_index_out;
	int &quality_row_index_out;
	int &samplerate_note_row_index_out;
};

QWidget *buildAudioGroup(RadioOutputConfigDialog *parent, const AudioGroupWidgets &w)
{
	auto *group = new QGroupBox(obs_module_text("RadioOutput.Audio.Group"), parent);
	auto *form = new QFormLayout(group);

	w.codec = new QComboBox(group);
	w.codec->addItem(obs_module_text("RadioOutput.Audio.Codec.Opus"), RADIO_CODEC_OPUS);
	w.codec->addItem(obs_module_text("RadioOutput.Audio.Codec.MP3"), RADIO_CODEC_MP3);
	w.codec->addItem(obs_module_text("RadioOutput.Audio.Codec.Vorbis"), RADIO_CODEC_VORBIS);
	form->addRow(obs_module_text("RadioOutput.Audio.Codec"), w.codec);

	/* MP3-only: channel mode (Stereo / Joint Stereo / Mono).  Hidden for
	 * Opus/Vorbis via onCodecChanged. */
	w.channel_mode = new QComboBox(group);
	w.channel_mode->addItem(obs_module_text("RadioOutput.Audio.ChannelMode.Stereo"), RADIO_CHANNEL_STEREO);
	w.channel_mode->addItem(obs_module_text("RadioOutput.Audio.ChannelMode.JointStereo"),
				RADIO_CHANNEL_JOINT_STEREO);
	w.channel_mode->addItem(obs_module_text("RadioOutput.Audio.ChannelMode.Mono"), RADIO_CHANNEL_MONO);
	w.channel_mode_row_index_out = form->rowCount();
	form->addRow(obs_module_text("RadioOutput.Audio.ChannelMode"), w.channel_mode);

	/* MP3-only: bitrate mode.  CBR/ABR use the Bitrate combo below as the
	 * fixed/average rate; VBR is quality-driven.  VBR Quality + Min/Max rows
	 * appear conditionally (see updateAudioVisibility). */
	w.bitrate_mode = new QComboBox(group);
	w.bitrate_mode->addItem(obs_module_text("RadioOutput.Audio.BitrateMode.CBR"), RADIO_BITRATE_CBR);
	w.bitrate_mode->addItem(obs_module_text("RadioOutput.Audio.BitrateMode.ABR"), RADIO_BITRATE_ABR);
	w.bitrate_mode->addItem(obs_module_text("RadioOutput.Audio.BitrateMode.VBR"), RADIO_BITRATE_VBR);
	w.bitrate_mode_row_index_out = form->rowCount();
	form->addRow(obs_module_text("RadioOutput.Audio.BitrateMode"), w.bitrate_mode);

	w.bitrate = new QComboBox(group);
	for (const int br : kBitrates) {
		w.bitrate->addItem(QString("%1 kbps").arg(br), br);
	}
	form->addRow(obs_module_text("RadioOutput.Audio.Bitrate"), w.bitrate);

	/* MP3 + VBR only: VBR quality 0 (best/largest) .. 9 (smallest). */
	w.vbr_quality = new QComboBox(group);
	for (int q = 0; q <= 9; ++q) {
		w.vbr_quality->addItem(QString::number(q), q);
	}
	w.vbr_quality_row_index_out = form->rowCount();
	form->addRow(obs_module_text("RadioOutput.Audio.VbrQuality"), w.vbr_quality);

	/* MP3 + VBR/ABR only: min / max bitrate bounds (reuse the bitrate list). */
	w.vbr_min = new QComboBox(group);
	w.vbr_max = new QComboBox(group);
	for (const int br : kBitrates) {
		w.vbr_min->addItem(QString("%1 kbps").arg(br), br);
		w.vbr_max->addItem(QString("%1 kbps").arg(br), br);
	}
	w.vbr_min_row_index_out = form->rowCount();
	form->addRow(obs_module_text("RadioOutput.Audio.VbrMin"), w.vbr_min);
	w.vbr_max_row_index_out = form->rowCount();
	form->addRow(obs_module_text("RadioOutput.Audio.VbrMax"), w.vbr_max);

	/* MP3-only: libmp3lame encoding quality 0 (best) .. 9 (fastest). */
	w.quality = new QComboBox(group);
	for (int q = 0; q <= 9; ++q) {
		w.quality->addItem(QString::number(q), q);
	}
	w.quality_row_index_out = form->rowCount();
	form->addRow(obs_module_text("RadioOutput.Audio.Quality"), w.quality);

	w.samplerate = new QComboBox(group);
	w.samplerate->addItem(obs_module_text("RadioOutput.Audio.Samplerate.MatchOBS"), 0);
	for (const int sr : kSamplerates) {
		w.samplerate->addItem(QString("%1 Hz").arg(sr), sr);
	}
	form->addRow(obs_module_text("RadioOutput.Audio.Samplerate"), w.samplerate);

	/* Opus-only advisory: shown when Codec = Opus, hidden otherwise (toggled
	 * by onCodecChanged).  Opus is intrinsically 48 kHz, so the selector above
	 * has no effect for it. */
	w.samplerate_note = new QLabel(obs_module_text("RadioOutput.Audio.Samplerate.OpusNote"), group);
	w.samplerate_note->setWordWrap(true);
	w.samplerate_note_row_index_out = form->rowCount();
	form->addRow(QString(), w.samplerate_note);

	w.form_out = form;
	return group;
}

QWidget *buildReconnectGroup(RadioOutputConfigDialog *parent, QCheckBox *&enabled, QSpinBox *&delay, QSpinBox *&max)
{
	auto *group = new QGroupBox(obs_module_text("RadioOutput.Reconnect.Group"), parent);
	auto *form = new QFormLayout(group);

	enabled = new QCheckBox(obs_module_text("RadioOutput.Reconnect.Enable"), group);
	form->addRow(enabled);

	delay = new QSpinBox(group);
	delay->setRange(1, 300);
	delay->setMinimumWidth(100);
	form->addRow(obs_module_text("RadioOutput.Reconnect.Delay"), delay);

	max = new QSpinBox(group);
	max->setRange(0, 999);
	max->setMinimumWidth(100);
	form->addRow(obs_module_text("RadioOutput.Reconnect.Max"), max);

	return group;
}

QWidget *buildIntegrationGroup(RadioOutputConfigDialog *parent, QCheckBox *&startWithStreaming)
{
	auto *group = new QGroupBox(obs_module_text("RadioOutput.Integration.Group"), parent);
	auto *form = new QFormLayout(group);

	startWithStreaming = new QCheckBox(obs_module_text("RadioOutput.StartWithStreaming"), group);
	form->addRow(startWithStreaming);

	return group;
}

/* Select the combo entry whose userData matches value; no-op if not present. */
void selectByData(QComboBox *combo, int value)
{
	const int idx = combo->findData(value);
	if (idx >= 0)
		combo->setCurrentIndex(idx);
}

} // namespace

RadioOutputConfigDialog::RadioOutputConfigDialog(obs_data_t *settings, QWidget *parent)
	: QDialog(parent),
	  settings_(settings)
{
	setWindowTitle(obs_module_text("RadioOutput.Config.Title"));
	setModal(true);
	setMinimumWidth(420);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(buildServerGroup(this, protocol_, host_, port_, mount_, password_, tls_enabled_, server_form_,
					   mount_row_index_, tls_row_index_));
	layout->addWidget(
		buildAudioGroup(this, {codec_, channel_mode_, bitrate_, bitrate_mode_, vbr_quality_, vbr_min_, vbr_max_,
				       quality_, samplerate_, samplerate_note_, audio_form_, channel_mode_row_index_,
				       bitrate_mode_row_index_, vbr_quality_row_index_, vbr_min_row_index_,
				       vbr_max_row_index_, quality_row_index_, samplerate_note_row_index_}));
	layout->addWidget(buildReconnectGroup(this, reconnect_enabled_, reconnect_delay_, reconnect_max_));
	layout->addWidget(buildIntegrationGroup(this, start_with_streaming_));

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &RadioOutputConfigDialog::onAccept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(protocol_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&RadioOutputConfigDialog::onProtocolChanged);
	connect(codec_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&RadioOutputConfigDialog::onCodecChanged);
	connect(bitrate_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&RadioOutputConfigDialog::onBitrateModeChanged);

	/* Populate widgets from the current settings. */
	selectByData(protocol_, (int)obs_data_get_int(settings_, SETTING_PROTOCOL));
	host_->setText(QString::fromUtf8(obs_data_get_string(settings_, SETTING_HOST)));
	port_->setValue((int)obs_data_get_int(settings_, SETTING_PORT));
	mount_->setText(QString::fromUtf8(obs_data_get_string(settings_, SETTING_MOUNT)));
	password_->setText(QString::fromUtf8(obs_data_get_string(settings_, SETTING_PASSWORD)));
	tls_enabled_->setChecked(obs_data_get_bool(settings_, SETTING_TLS));
	selectByData(codec_, (int)obs_data_get_int(settings_, SETTING_CODEC));
	selectByData(channel_mode_, (int)obs_data_get_int(settings_, SETTING_CHANNEL_MODE));
	selectByData(bitrate_, (int)obs_data_get_int(settings_, SETTING_BITRATE));
	selectByData(bitrate_mode_, (int)obs_data_get_int(settings_, SETTING_BITRATE_MODE));
	selectByData(vbr_quality_, (int)obs_data_get_int(settings_, SETTING_VBR_QUALITY));
	selectByData(vbr_min_, (int)obs_data_get_int(settings_, SETTING_VBR_MIN_BITRATE));
	selectByData(vbr_max_, (int)obs_data_get_int(settings_, SETTING_VBR_MAX_BITRATE));
	selectByData(quality_, (int)obs_data_get_int(settings_, SETTING_LAME_QUALITY));
	selectByData(samplerate_, (int)obs_data_get_int(settings_, SETTING_STREAM_SAMPLERATE));
	reconnect_enabled_->setChecked(obs_data_get_bool(settings_, SETTING_RECONNECT));
	reconnect_delay_->setValue((int)obs_data_get_int(settings_, SETTING_RECONNECT_DELAY));
	reconnect_max_->setValue((int)obs_data_get_int(settings_, SETTING_RECONNECT_MAX));
	start_with_streaming_->setChecked(obs_data_get_bool(settings_, SETTING_START_WITH_STREAMING));

	/* Apply initial mount visibility based on the loaded protocol. */
	onProtocolChanged(protocol_->currentIndex());
	/* Apply initial Opus-note visibility based on the loaded codec. */
	onCodecChanged(codec_->currentIndex());
}

void RadioOutputConfigDialog::onProtocolChanged(int /*index*/)
{
	/* SHOUTcast v1 has neither a mount path nor TLS support; hide both
	 * rows so the UI matches what the protocol actually offers.  Values
	 * are preserved in the obs_data_t so toggling back to Icecast
	 * restores the previous mount/TLS choice without re-typing. */
	const bool is_shoutcast = (protocol_->currentData().toInt() == RADIO_PROTOCOL_SHOUTCAST);
	server_form_->setRowVisible(mount_row_index_, !is_shoutcast);
	server_form_->setRowVisible(tls_row_index_, !is_shoutcast);
}

void RadioOutputConfigDialog::onCodecChanged(int /*index*/)
{
	updateAudioVisibility();
}

void RadioOutputConfigDialog::onBitrateModeChanged(int /*index*/)
{
	updateAudioVisibility();
}

void RadioOutputConfigDialog::updateAudioVisibility()
{
	const int codec = codec_->currentData().toInt();
	const bool is_opus = (codec == RADIO_CODEC_OPUS);
	const bool is_mp3 = (codec == RADIO_CODEC_MP3);
	const int mode = bitrate_mode_->currentData().toInt();
	const bool is_vbr = (mode == RADIO_BITRATE_VBR);
	const bool is_abr = (mode == RADIO_BITRATE_ABR);

	/* Channel mode, quality, and bitrate mode are libmp3lame knobs — only
	 * meaningful for MP3.  Hidden values are preserved in the obs_data_t. */
	audio_form_->setRowVisible(channel_mode_row_index_, is_mp3);
	audio_form_->setRowVisible(quality_row_index_, is_mp3);
	audio_form_->setRowVisible(bitrate_mode_row_index_, is_mp3);
	/* VBR Quality only applies to VBR; Min/Max bound both VBR and ABR. */
	audio_form_->setRowVisible(vbr_quality_row_index_, is_mp3 && is_vbr);
	audio_form_->setRowVisible(vbr_min_row_index_, is_mp3 && (is_vbr || is_abr));
	audio_form_->setRowVisible(vbr_max_row_index_, is_mp3 && (is_vbr || is_abr));
	/* The samplerate note explains Opus's fixed 48 kHz. */
	audio_form_->setRowVisible(samplerate_note_row_index_, is_opus);
}

void RadioOutputConfigDialog::onAccept()
{
	obs_data_set_int(settings_, SETTING_PROTOCOL, protocol_->currentData().toInt());
	obs_data_set_string(settings_, SETTING_HOST, host_->text().toUtf8().constData());
	obs_data_set_int(settings_, SETTING_PORT, port_->value());
	obs_data_set_string(settings_, SETTING_MOUNT, mount_->text().toUtf8().constData());
	obs_data_set_string(settings_, SETTING_PASSWORD, password_->text().toUtf8().constData());
	obs_data_set_bool(settings_, SETTING_TLS, tls_enabled_->isChecked());
	obs_data_set_int(settings_, SETTING_CODEC, codec_->currentData().toInt());
	obs_data_set_int(settings_, SETTING_CHANNEL_MODE, channel_mode_->currentData().toInt());
	obs_data_set_int(settings_, SETTING_BITRATE, bitrate_->currentData().toInt());
	obs_data_set_int(settings_, SETTING_BITRATE_MODE, bitrate_mode_->currentData().toInt());
	obs_data_set_int(settings_, SETTING_VBR_QUALITY, vbr_quality_->currentData().toInt());
	obs_data_set_int(settings_, SETTING_VBR_MIN_BITRATE, vbr_min_->currentData().toInt());
	obs_data_set_int(settings_, SETTING_VBR_MAX_BITRATE, vbr_max_->currentData().toInt());
	obs_data_set_int(settings_, SETTING_LAME_QUALITY, quality_->currentData().toInt());
	obs_data_set_int(settings_, SETTING_STREAM_SAMPLERATE, samplerate_->currentData().toInt());
	obs_data_set_bool(settings_, SETTING_RECONNECT, reconnect_enabled_->isChecked());
	obs_data_set_int(settings_, SETTING_RECONNECT_DELAY, reconnect_delay_->value());
	obs_data_set_int(settings_, SETTING_RECONNECT_MAX, reconnect_max_->value());
	obs_data_set_bool(settings_, SETTING_START_WITH_STREAMING, start_with_streaming_->isChecked());

	accept();
}
