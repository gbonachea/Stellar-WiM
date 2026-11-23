#pragma once

#include <QJsonObject>
#include <QMap>
#include <QWidget>

#include "stellaripcclient.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

/*!
 * Interfaz de la Stellar Configuration API.
 *
 * El mismo widget se usa dentro de Stellar Settings (Gravity) y en la
 * aplicación independiente stellar-wim-settings. Todas las lecturas y
 * escrituras pasan por StellarIpcClient; nada edita rc.xml directamente.
 */
class StellarWimConfigWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StellarWimConfigWidget(QWidget *parent = nullptr);

private slots:
    void refresh();
    void restoreDefaults();
    void manageKeybindings();
    void manageRules();

private:
    void buildUi();
    QCheckBox *addToggle(QFormLayout *layout, const QString &label,
                         const char *key);
    QSpinBox *addInteger(QFormLayout *layout, const QString &label,
                         const char *key, int minimum, int maximum);
    QDoubleSpinBox *addNumber(QFormLayout *layout, const QString &label,
                              const char *key, double minimum, double maximum);
    QComboBox *addChoice(QFormLayout *layout, const QString &label,
                         const char *key, const QStringList &choices);
    void setControlValue(const char *key, const QJsonValue &value);
    void writeValue(const char *key, const QString &value);
    void sendNextValue();
    void setStatus(const QString &message, bool error = false);

    StellarIpcClient *m_client = nullptr;
    QJsonObject m_values;
    QLabel *m_status = nullptr;
    QLineEdit *m_themeName = nullptr;
    bool m_loading = false;
    bool m_writing = false;
    QMap<QString, QString> m_pendingValues;
};
