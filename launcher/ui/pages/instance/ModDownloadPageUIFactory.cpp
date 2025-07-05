#include "ModDownloadPageUIFactory.h"
#include "ModDownloadPageStyles.h"
#include "Application.h"
#include <QIcon>
#include <QSizePolicy>

QIcon ModDownloadPageUIFactory::ModDownloadInfo::getDefaultIcon() const
{
    return APPLICATION->getThemedIcon("screenshot-placeholder");
}

QWidget *ModDownloadPageUIFactory::createModItemWidget()
{
    QWidget *widget = new QWidget();
    widget->setObjectName("modItemWidget");
    widget->setMinimumSize(600, ModDownloadPageStyles::MOD_ITEM_MIN_HEIGHT);
    widget->setMaximumSize(16777215, ModDownloadPageStyles::MOD_ITEM_MAX_HEIGHT);
    widget->setStyleSheet(ModDownloadPageStyles::getModItemWidgetStyle());
    return widget;
}

QFrame *ModDownloadPageUIFactory::createIconFrame()
{
    QFrame *frame = new QFrame();
    frame->setObjectName("iconFrame");
    frame->setMinimumSize(ModDownloadPageStyles::ICON_SIZE, ModDownloadPageStyles::ICON_SIZE);
    frame->setMaximumSize(ModDownloadPageStyles::ICON_SIZE, ModDownloadPageStyles::ICON_SIZE);
    frame->setFrameShape(QFrame::NoFrame);
    frame->setStyleSheet(ModDownloadPageStyles::getIconFrameStyle());
    return frame;
}

QLabel *ModDownloadPageUIFactory::createIconLabel()
{
    QLabel *label = new QLabel();
    label->setObjectName("iconLabel");
    label->setAlignment(Qt::AlignCenter);
    return label;
}

QLabel *ModDownloadPageUIFactory::createTitleLabel(const QString &title)
{
    QLabel *label = new QLabel(title);
    label->setObjectName("titleLabel");
    label->setStyleSheet(ModDownloadPageStyles::getTitleLabelStyle());
    return label;
}

QLabel *ModDownloadPageUIFactory::createAuthorLabel(const QString &author)
{
    QLabel *label = new QLabel(author);
    label->setObjectName("authorLabel");
    label->setStyleSheet(ModDownloadPageStyles::getAuthorLabelStyle());
    return label;
}

QLabel *ModDownloadPageUIFactory::createDescriptionLabel(const QString &description)
{
    QLabel *label = new QLabel();
    label->setObjectName("descriptionLabel");
    label->setWordWrap(false);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setToolTip(description);
    label->setStyleSheet(ModDownloadPageStyles::getDescriptionLabelStyle());

    // 设置省略文本
    QString elidedText = label->fontMetrics().elidedText(description, Qt::ElideRight, 400);
    label->setText(elidedText);

    return label;
}

QLabel *ModDownloadPageUIFactory::createStatsLabel(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setStyleSheet(ModDownloadPageStyles::getStatsLabelStyle());
    return label;
}

QProgressBar *ModDownloadPageUIFactory::createProgressBar()
{
    QProgressBar *progressBar = new QProgressBar();
    progressBar->setMinimum(0);
    progressBar->setMaximum(100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    progressBar->setFormat("%p%");
    progressBar->setFixedHeight(ModDownloadPageStyles::PROGRESS_BAR_HEIGHT);
    progressBar->setStyleSheet(ModDownloadPageStyles::getProgressBarStyle());
    progressBar->hide();
    return progressBar;
}

QPushButton *ModDownloadPageUIFactory::createInstallButton(const QString &text)
{
    QPushButton *button = new QPushButton(text);
    button->setObjectName("installButton");
    button->setMinimumSize(ModDownloadPageStyles::BUTTON_WIDTH, ModDownloadPageStyles::BUTTON_HEIGHT);
    button->setFixedWidth(ModDownloadPageStyles::BUTTON_WIDTH);
    button->setStyleSheet(ModDownloadPageStyles::getInstallButtonStyle());
    return button;
}

QLabel *ModDownloadPageUIFactory::createLoadingLabel(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(ModDownloadPageStyles::getLoadingLabelStyle());
    return label;
}

// 布局创建方法实现
QHBoxLayout *ModDownloadPageUIFactory::createMainLayout(QWidget *parent)
{
    QHBoxLayout *layout = new QHBoxLayout(parent);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(15);
    return layout;
}

QVBoxLayout *ModDownloadPageUIFactory::createIconLayout(QFrame *parent)
{
    QVBoxLayout *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    return layout;
}

QVBoxLayout *ModDownloadPageUIFactory::createContentLayout()
{
    QVBoxLayout *layout = new QVBoxLayout();
    layout->setSpacing(4);
    return layout;
}

QHBoxLayout *ModDownloadPageUIFactory::createTitleLayout()
{
    QHBoxLayout *layout = new QHBoxLayout();
    return layout;
}

QHBoxLayout *ModDownloadPageUIFactory::createStatsLayout()
{
    QHBoxLayout *layout = new QHBoxLayout();
    layout->setSpacing(8);
    return layout;
}

QVBoxLayout *ModDownloadPageUIFactory::createButtonLayout()
{
    QVBoxLayout *layout = new QVBoxLayout();
    return layout;
}