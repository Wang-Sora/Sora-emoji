#include "pages/SettingsPage.h"

#include "app/ThemeManager.h"
#include "core/AiClient.h"
#include "core/TintedIcon.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <iterator>

namespace {

QLabel *makeFieldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("FieldLabel"));
    return label;
}

// 预置服务商：baseUrl/model 为空表示「不覆盖」，对应「自定义」项。
// 所有厂商都走 OpenAI 兼容协议，所以只需这几个字段的差异。
struct ProviderPreset {
    QString id;
    QString name;
    QString baseUrl;
    QString model;
};

const ProviderPreset kProviders[] = {
    { QStringLiteral("openai"),    QStringLiteral("OpenAI"),       QStringLiteral("https://api.openai.com/v1"),         QStringLiteral("gpt-4o-mini") },
    { QStringLiteral("deepseek"),  QStringLiteral("DeepSeek"),     QStringLiteral("https://api.deepseek.com/v1"),        QStringLiteral("deepseek-chat") },
    { QStringLiteral("moonshot"),  QStringLiteral("月之暗面 Kimi"), QStringLiteral("https://api.moonshot.cn/v1"),         QStringLiteral("moonshot-v1-8k") },
    { QStringLiteral("dashscope"), QStringLiteral("通义千问"),      QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1"), QStringLiteral("qwen-plus") },
    { QStringLiteral("zhipu"),     QStringLiteral("智谱 GLM"),      QStringLiteral("https://open.bigmodel.cn/api/paas/v4"), QStringLiteral("glm-4-flash") },
    { QStringLiteral("ollama"),    QStringLiteral("本地 Ollama"),   QStringLiteral("http://localhost:11434/v1"),          QStringLiteral("qwen2.5:7b") },
    { QStringLiteral("custom"),    QStringLiteral("自定义"),        QString(),                                            QString() },
};

const ProviderPreset *findProvider(const QString &id)
{
    for (const ProviderPreset &preset : kProviders) {
        if (preset.id == id)
            return &preset;
    }
    return nullptr;
}

int providerIndex(const QString &id)
{
    for (int i = 0; i < int(std::size(kProviders)); ++i) {
        if (kProviders[i].id == id)
            return i;
    }
    return -1;
}

} // namespace

SettingsPage::SettingsPage(AiClient *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
{
    buildUi();
    loadSettings();
    retint();
}

void SettingsPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("PageHeader"));
    header->setFixedHeight(52);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 0, 20, 0);

    auto *title = new QLabel(QStringLiteral("设置"), header);
    title->setObjectName(QStringLiteral("SectionTitle"));
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);
    root->addWidget(header);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("ChatScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(28, 22, 28, 22);
    contentLayout->setSpacing(18);

    // ---- 外观 ----
    auto *appearance = new QFrame(content);
    appearance->setObjectName(QStringLiteral("Card"));
    auto *appearanceLayout = new QFormLayout(appearance);
    appearanceLayout->setContentsMargins(20, 18, 20, 18);
    appearanceLayout->setSpacing(12);
    appearanceLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *appearanceTitle = new QLabel(QStringLiteral("外观"), appearance);
    appearanceTitle->setObjectName(QStringLiteral("CardTitle"));
    appearanceLayout->addRow(appearanceTitle);

    m_theme = new QComboBox(appearance);
    m_theme->addItem(QStringLiteral("深色"), static_cast<int>(ThemeManager::Mode::Dark));
    m_theme->addItem(QStringLiteral("浅色"), static_cast<int>(ThemeManager::Mode::Light));

    m_themeToggle = new QToolButton(appearance);
    m_themeToggle->setObjectName(QStringLiteral("Ghost"));
    m_themeToggle->setCursor(Qt::PointingHandCursor);
    m_themeToggle->setFixedSize(30, 30);
    m_themeToggle->setIconSize(QSize(16, 16));
    m_themeToggle->setToolTip(QStringLiteral("一键切换主题"));

    auto *themeRow = new QWidget(appearance);
    auto *themeRowLayout = new QHBoxLayout(themeRow);
    themeRowLayout->setContentsMargins(0, 0, 0, 0);
    themeRowLayout->addWidget(m_theme, 1);
    themeRowLayout->addWidget(m_themeToggle);

    appearanceLayout->addRow(makeFieldLabel(QStringLiteral("主题"), appearance), themeRow);

    contentLayout->addWidget(appearance);

    // ---- AI 接入 ----
    auto *ai = new QFrame(content);
    ai->setObjectName(QStringLiteral("Card"));
    auto *aiLayout = new QFormLayout(ai);
    aiLayout->setContentsMargins(20, 18, 20, 18);
    aiLayout->setSpacing(12);
    aiLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *aiTitle = new QLabel(QStringLiteral("AI 接入（OpenAI 兼容接口）"), ai);
    aiTitle->setObjectName(QStringLiteral("CardTitle"));
    aiLayout->addRow(aiTitle);

    m_provider = new QComboBox(ai);
    for (const ProviderPreset &preset : kProviders)
        m_provider->addItem(preset.name, preset.id);
    aiLayout->addRow(makeFieldLabel(QStringLiteral("服务商"), ai), m_provider);

    m_baseUrl = new QLineEdit(ai);
    m_baseUrl->setPlaceholderText(QStringLiteral("https://api.deepseek.com/v1"));
    aiLayout->addRow(makeFieldLabel(QStringLiteral("Base URL"), ai), m_baseUrl);

    m_apiKey = new QLineEdit(ai);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("sk-…"));
    aiLayout->addRow(makeFieldLabel(QStringLiteral("API Key"), ai), m_apiKey);

    m_model = new QLineEdit(ai);
    m_model->setPlaceholderText(QStringLiteral("deepseek-chat"));
    aiLayout->addRow(makeFieldLabel(QStringLiteral("模型"), ai), m_model);

    m_systemPrompt = new QTextEdit(ai);
    m_systemPrompt->setObjectName(QStringLiteral("SystemPrompt"));
    m_systemPrompt->setPlaceholderText(QStringLiteral("给助手设定一个人格与风格"));
    m_systemPrompt->setMinimumHeight(88);
    aiLayout->addRow(makeFieldLabel(QStringLiteral("系统提示词"), ai), m_systemPrompt);

    contentLayout->addWidget(ai);

    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(12);

    m_save = new QToolButton(content);
    m_save->setObjectName(QStringLiteral("Primary"));
    m_save->setText(QStringLiteral("  保存"));
    m_save->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_save->setCursor(Qt::PointingHandCursor);
    m_save->setMinimumHeight(34);
    actions->addWidget(m_save);

    m_status = new QLabel(content);
    m_status->setObjectName(QStringLiteral("Hint"));
    actions->addWidget(m_status);
    actions->addStretch(1);

    contentLayout->addLayout(actions);
    contentLayout->addStretch(1);

    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    connect(m_save, &QToolButton::clicked, this, [this] {
        saveSettings();
        applyToClient();
        m_status->setText(QStringLiteral("已保存"));
    });

    connect(m_theme, &QComboBox::currentIndexChanged, this, [this](int) {
        ThemeManager::instance().setMode(
            static_cast<ThemeManager::Mode>(m_theme->currentData().toInt()));
    });

    connect(m_themeToggle, &QToolButton::clicked, this, [this] {
        ThemeManager::instance().toggle();
        m_theme->setCurrentIndex(ThemeManager::instance().isDark() ? 0 : 1);
    });

    // 选中预置项就回填 baseUrl/model，但绝不动 API Key（换厂商不等于换密钥）
    connect(m_provider, &QComboBox::currentIndexChanged, this, [this](int index) {
        const ProviderPreset *preset =
            findProvider(m_provider->itemData(index).toString());
        if (!preset || preset->baseUrl.isEmpty())
            return;
        m_baseUrl->setText(preset->baseUrl);
        m_model->setText(preset->model);
    });

    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, &SettingsPage::retint);
}

void SettingsPage::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ai"));

    const AiClient::Config config = m_client->config();

    // 先定下拉框：用 blocker 抑制信号，否则会触发回填覆盖下面恢复的值
    const QString providerId =
        settings.value(QStringLiteral("providerId"), config.providerId).toString();
    int providerAt = providerIndex(providerId);
    if (providerAt < 0)
        providerAt = providerIndex(QStringLiteral("custom"));
    {
        const QSignalBlocker blocker(m_provider);
        m_provider->setCurrentIndex(providerAt);
    }

    m_baseUrl->setText(settings.value(QStringLiteral("baseUrl"), config.baseUrl).toString());
    m_apiKey->setText(settings.value(QStringLiteral("apiKey")).toString());
    m_model->setText(settings.value(QStringLiteral("model"), config.model).toString());
    m_systemPrompt->setPlainText(
        settings.value(QStringLiteral("systemPrompt"), config.systemPrompt).toString());

    settings.endGroup();

    m_theme->setCurrentIndex(ThemeManager::instance().isDark() ? 0 : 1);

    applyToClient();
}

void SettingsPage::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ai"));
    settings.setValue(QStringLiteral("providerId"), m_provider->currentData().toString());
    settings.setValue(QStringLiteral("baseUrl"), m_baseUrl->text().trimmed());
    settings.setValue(QStringLiteral("apiKey"), m_apiKey->text().trimmed());
    settings.setValue(QStringLiteral("model"), m_model->text().trimmed());
    settings.setValue(QStringLiteral("systemPrompt"), m_systemPrompt->toPlainText());
    settings.endGroup();
}

void SettingsPage::applyToClient()
{
    AiClient::Config config = m_client->config();
    config.providerId = m_provider->currentData().toString();
    config.baseUrl = m_baseUrl->text().trimmed();
    config.apiKey = m_apiKey->text().trimmed();
    config.model = m_model->text().trimmed();
    config.systemPrompt = m_systemPrompt->toPlainText();

    if (config.baseUrl.isEmpty())
        config.baseUrl = QStringLiteral("https://api.openai.com/v1");
    if (config.model.isEmpty())
        config.model = QStringLiteral("gpt-4o-mini");

    m_client->setConfig(config);
}

void SettingsPage::retint()
{
    const bool dark = ThemeManager::instance().isDark();

    m_themeToggle->setIcon(TintedIcon::create(
        dark ? QStringLiteral(":/icons/sun.svg") : QStringLiteral(":/icons/moon.svg"),
        ThemeManager::instance().iconIdle(), 16));

    m_save->setIcon(TintedIcon::create(QStringLiteral(":/icons/settings.svg"),
                                       QColor(QStringLiteral("#052E18")), 16));
}
