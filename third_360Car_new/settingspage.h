#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

namespace Ui {
class SettingsPage;
}

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage();

    void loadConfig();
    void saveConfig();

signals:
    void settingsSaved();

private slots:
    void onSaveClicked();
    void onConnected();
    void onConnectionError(const QString& error);

private:
    Ui::SettingsPage *ui;
    const QString m_configFile = "config.ini";
};

#endif // SETTINGSPAGE_H
