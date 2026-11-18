// Settings dialog: station, sound card, PTT, transmit / AFC options, character format and the macro editor.
// The layout lives in SettingsDialog.ui.
#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <memory>

#include "AppSettings.h"

namespace Ui { class SettingsDialog; }

class SettingsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit SettingsDialog(const AppSettings &s, QWidget *parent = nullptr);
	~SettingsDialog() override;

	// Copies the dialog values into 's' (only the fields this dialog owns).
	void apply(AppSettings &s) const;

private:
	void refreshPorts();

	std::unique_ptr<Ui::SettingsDialog> ui;
};

#endif
