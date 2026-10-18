#ifndef SORA_SETTINGSPAGE_H
#define SORA_SETTINGSPAGE_H

#include <QWidget>

class AiClient;
class QComboBox;
class QLabel;
class QLineEdit;
class QTextEdit;
class QToolButton;

/**
 * 设置页：通用行为 + 主题切换 + AI 接入配置。
 *
 * 配置用 QSettings 持久化（注册表 / ini），API Key 只存在本机，
 * 不会写进任何代码或仓库文件。
 */
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(AiClient *client, QWidget *parent = nullptr);

    void retint();

private:
    void buildUi();
    void loadSettings();
    void saveSettings();
    void applyToClient();

    AiClient *m_client = nullptr;

    QComboBox *m_closeAction = nullptr;
    QComboBox *m_theme = nullptr;
    QComboBox *m_provider = nullptr;
    QLineEdit *m_baseUrl = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QLineEdit *m_model = nullptr;
    QTextEdit *m_systemPrompt = nullptr;
    QLabel *m_status = nullptr;
    QToolButton *m_save = nullptr;
    QToolButton *m_themeToggle = nullptr;
};

#endif // SORA_SETTINGSPAGE_H
