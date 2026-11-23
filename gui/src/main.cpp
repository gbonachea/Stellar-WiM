#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

#include "stellarwimconfigwidget.h"

static bool isGravity()
{
    const auto desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP")
                              .split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const auto &desktop : desktops) {
        if (desktop.compare(QLatin1String("Gravity"), Qt::CaseInsensitive) == 0
            || desktop.compare(QLatin1String("Stellar-Gravity"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("stellar-wim-settings"));
    QApplication::setApplicationDisplayName(QStringLiteral("Stellar WiM Settings"));
    QApplication::setOrganizationName(QStringLiteral("Stellar"));
    if (isGravity()) {
        if (auto *style = QStyleFactory::create(QStringLiteral("Fusion")))
            app.setStyle(style);
        QPalette palette = app.palette();
        palette.setColor(QPalette::Window, QColor("#0b2452"));
        palette.setColor(QPalette::WindowText, QColor("#e8f2ff"));
        palette.setColor(QPalette::Base, QColor("#0b2452"));
        palette.setColor(QPalette::AlternateBase, QColor("#174a8b"));
        palette.setColor(QPalette::Text, QColor("#e8f2ff"));
        palette.setColor(QPalette::Button, QColor("#174a8b"));
        palette.setColor(QPalette::ButtonText, QColor("#e8f2ff"));
        palette.setColor(QPalette::Highlight, QColor("#2a7cf0"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        app.setPalette(palette);
        app.setStyleSheet(QStringLiteral(
            "QPushButton, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {"
            "border: 1px solid #8fcfe7; border-radius: 7px; padding: 6px; }"
            "QPushButton:hover { background: #2a7cf0; }"));
    }
    StellarWimConfigWidget widget;
    widget.setWindowIcon(QIcon::fromTheme(QStringLiteral("preferences-system-windows")));
    widget.setWindowTitle(QStringLiteral("Stellar WiM Settings"));
    widget.resize(760, 640);
    widget.show();
    return app.exec();
}
