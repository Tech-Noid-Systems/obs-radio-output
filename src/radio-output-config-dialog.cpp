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

QWidget *buildServerGroup(RadioOutputConfigDialog *parent, QLineEdit *&host, QSpinBox *&port, QLineEdit *&mount,
			  QLineEdit *&password)
{
	auto *group = new QGroupBox(obs_module_text("RadioOutput.Server.Group"), parent);
	auto *form = new QFormLayout(group);

	host = new QLineEdit(group);
	form->addRow(obs_module_text("RadioOutput.Server.Host"), host);

	port = new QSpinBox(group);
	port->setRange(1, 65535);
	form->addRow(obs_module_text("RadioOutput.Server.Port"), port);

	mount = new QLineEdit(group);
	form->addRow(obs_module_text("RadioOutput.Server.Mount"), mount);

	password = new QLineEdit(group);
	password->setEchoMode(QLineEdit::Password);
	form->addRow(obs_module_text("RadioOutput.Server.Password"), password);

	return group;
}

QWidget *buildAudioGroup(RadioOutputConfigDialog *parent, QComboBox *&codec, QComboBox *&bitrate)
{
	auto *group = new QGroupBox(obs_module_text("RadioOutput.Audio.Group"), parent);
	auto *form = new QFormLayout(group);

	codec = new QComboBox(group);
	codec->addItem(obs_module_text("RadioOutput.Audio.Codec.Opus"), RADIO_CODEC_OPUS);
	codec->addItem(obs_module_text("RadioOutput.Audio.Codec.MP3"), RADIO_CODEC_MP3);
	form->addRow(obs_module_text("RadioOutput.Audio.Codec"), codec);

	bitrate = new QComboBox(group);
	for (int br : kBitrates) {
		bitrate->addItem(QString("%1 kbps").arg(br), br);
	}
	form->addRow(obs_module_text("RadioOutput.Audio.Bitrate"), bitrate);

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
	form->addRow(obs_module_text("RadioOutput.Reconnect.Delay"), delay);

	max = new QSpinBox(group);
	max->setRange(0, 999);
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
	int idx = combo->findData(value);
	if (idx >= 0)
		combo->setCurrentIndex(idx);
}

} // namespace

RadioOutputConfigDialog::RadioOutputConfigDialog(obs_data_t *settings, QWidget *parent) : QDialog(parent), settings_(settings)
{
	setWindowTitle(obs_module_text("RadioOutput.Config.Title"));
	setModal(true);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(buildServerGroup(this, host_, port_, mount_, password_));
	layout->addWidget(buildAudioGroup(this, codec_, bitrate_));
	layout->addWidget(buildReconnectGroup(this, reconnect_enabled_, reconnect_delay_, reconnect_max_));
	layout->addWidget(buildIntegrationGroup(this, start_with_streaming_));

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &RadioOutputConfigDialog::onAccept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	/* Populate widgets from the current settings. */
	host_->setText(QString::fromUtf8(obs_data_get_string(settings_, SETTING_HOST)));
	port_->setValue((int)obs_data_get_int(settings_, SETTING_PORT));
	mount_->setText(QString::fromUtf8(obs_data_get_string(settings_, SETTING_MOUNT)));
	password_->setText(QString::fromUtf8(obs_data_get_string(settings_, SETTING_PASSWORD)));
	selectByData(codec_, (int)obs_data_get_int(settings_, SETTING_CODEC));
	selectByData(bitrate_, (int)obs_data_get_int(settings_, SETTING_BITRATE));
	reconnect_enabled_->setChecked(obs_data_get_bool(settings_, SETTING_RECONNECT));
	reconnect_delay_->setValue((int)obs_data_get_int(settings_, SETTING_RECONNECT_DELAY));
	reconnect_max_->setValue((int)obs_data_get_int(settings_, SETTING_RECONNECT_MAX));
	start_with_streaming_->setChecked(obs_data_get_bool(settings_, SETTING_START_WITH_STREAMING));
}

void RadioOutputConfigDialog::onAccept()
{
	obs_data_set_string(settings_, SETTING_HOST, host_->text().toUtf8().constData());
	obs_data_set_int(settings_, SETTING_PORT, port_->value());
	obs_data_set_string(settings_, SETTING_MOUNT, mount_->text().toUtf8().constData());
	obs_data_set_string(settings_, SETTING_PASSWORD, password_->text().toUtf8().constData());
	obs_data_set_int(settings_, SETTING_CODEC, codec_->currentData().toInt());
	obs_data_set_int(settings_, SETTING_BITRATE, bitrate_->currentData().toInt());
	obs_data_set_bool(settings_, SETTING_RECONNECT, reconnect_enabled_->isChecked());
	obs_data_set_int(settings_, SETTING_RECONNECT_DELAY, reconnect_delay_->value());
	obs_data_set_int(settings_, SETTING_RECONNECT_MAX, reconnect_max_->value());
	obs_data_set_bool(settings_, SETTING_START_WITH_STREAMING, start_with_streaming_->isChecked());

	accept();
}
