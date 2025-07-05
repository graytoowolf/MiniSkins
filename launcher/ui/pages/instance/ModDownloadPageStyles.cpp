#include "ModDownloadPageStyles.h"

QString ModDownloadPageStyles::getModItemWidgetStyle()
{
    return "QWidget#modItemWidget {"
           "    background-color: palette(base);"
           "    border: 1px solid palette(mid);"
           "    border-radius: 8px;"
           "    margin: 2px;"
           "}"
           "QWidget#modItemWidget:hover {"
           "    border: 2px solid palette(highlight);"
           "    background-color: palette(alternate-base);"
           "}";
}

QString ModDownloadPageStyles::getIconFrameStyle()
{
    return "QFrame#iconFrame {"
           "    border: 2px solid palette(light);"
           "    border-radius: 10px;"
           "    margin: 2px;"
           "}"
           "QFrame#iconFrame:hover {"
           "    border: 2px solid palette(highlight);"
           "}";
}

QString ModDownloadPageStyles::getTitleLabelStyle()
{
    return "QLabel#titleLabel {"
           "    font-size: 14px;"
           "    font-weight: bold;"
           "    color: palette(window-text);"
           "    background-color: transparent;"
           "    margin-bottom: 2px;"
           "}";
}

QString ModDownloadPageStyles::getAuthorLabelStyle()
{
    return "QLabel#authorLabel {"
           "    font-size: 11px;"
           "    color: palette(mid);"
           "    background-color: transparent;"
           "    font-style: italic;"
           "}";
}

QString ModDownloadPageStyles::getDescriptionLabelStyle()
{
    return "QLabel#descriptionLabel {"
           "    font-size: 12px;"
           "    color: palette(window-text);"
           "    background-color: transparent;"
           "    margin: 4px 2px;"
           "    padding: 4px 2px;"
           "    min-height: 16px;"
           "}";
}

QString ModDownloadPageStyles::getStatsLabelStyle()
{
    return "font-size: 11px;"
           "color: palette(window-text);"
           "background-color: palette(light);"
           "border-radius: 3px;"
           "padding: 2px 6px;"
           "margin: 1px;";
}

QString ModDownloadPageStyles::getProgressBarStyle()
{
    return "QProgressBar {"
           "    background-color: palette(base);"
           "    border: 1px solid palette(mid);"
           "    border-radius: 8px;"
           "    text-align: center;"
           "    font-size: 11px;"
           "    color: palette(window-text);"
           "}"
           "QProgressBar::chunk {"
           "    background-color: palette(highlight);"
           "    border-radius: 6px;"
           "    margin: 1px;"
           "}";
}

QString ModDownloadPageStyles::getInstallButtonStyle()
{
    return "QPushButton#installButton {"
           "    background-color: palette(highlight);"
           "    color: palette(highlighted-text);"
           "    border: 1px solid palette(highlight);"
           "    border-radius: 6px;"
           "    font-weight: bold;"
           "    font-size: 12px;"
           "    padding: 4px 12px;"
           "}"
           "QPushButton#installButton:hover {"
           "    background-color: palette(light);"
           "    color: palette(dark);"
           "    border: 2px solid palette(highlight);"
           "}"
           "QPushButton#installButton:pressed {"
           "    background-color: palette(dark);"
           "    color: palette(bright-text);"
           "}"
           "QPushButton#installButton:disabled {"
           "    background-color: palette(mid);"
           "    color: palette(window-text);"
           "    border: 1px solid palette(mid);"
           "    font-weight: bold;"
           "}";
}

QString ModDownloadPageStyles::getLoadingLabelStyle()
{
    return "font-size: 12px; color: #999999; padding: 10px;";
}