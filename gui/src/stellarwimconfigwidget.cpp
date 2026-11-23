#include "stellarwimconfigwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
QWidget *groupBox(const QString &title, QFormLayout **form)
{
    auto *box = new QGroupBox(title);
    *form = new QFormLayout(box);
    (*form)->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return box;
}
} // namespace

StellarWimConfigWidget::StellarWimConfigWidget(QWidget *parent)
    : QWidget(parent)
{
    m_client = new StellarIpcClient(this);
    buildUi();

    connect(m_client, &StellarIpcClient::eventReceived, this,
            [this](const QJsonObject &event) {
        if (event.value(QStringLiteral("type")).toString() == QLatin1String("config")
            && !m_writing) {
            refresh();
        }
    });
    m_client->subscribe([](const QJsonObject &) {});
    QTimer::singleShot(0, this, &StellarWimConfigWidget::refresh);
}

void StellarWimConfigWidget::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(m_status);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    scroll->setWidget(content);
    outer->addWidget(scroll);

    QFormLayout *form = nullptr;

    auto *effects = groupBox(tr("Animaciones y efectos"), &form);
    addToggle(form, tr("Animaciones"), "effects.animations");
    addToggle(form, tr("Opacidad"), "effects.opacity");
    addNumber(form, tr("Nivel de opacidad"), "effects.opacity-level", 0.10, 1.00);
    addToggle(form, tr("Sombras"), "effects.shadows");
    layout->addWidget(effects);

    auto *decoration = groupBox(tr("Decoraciones y tema"), &form);
    addInteger(form, tr("Radio de esquinas"), "theme.corner-radius", 0, 32);
    m_themeName = new QLineEdit(content);
    m_themeName->setPlaceholderText(tr("Tema interno (vacío)"));
    form->addRow(tr("Nombre del tema"), m_themeName);
    connect(m_themeName, &QLineEdit::editingFinished, this, [this] {
        writeValue("theme.name", m_themeName->text());
    });
    addToggle(form, tr("Mostrar título"), "theme.show-title");
    addToggle(form, tr("Conservar borde"), "theme.keep-border");
    layout->addWidget(decoration);

    auto *windows = groupBox(tr("Pestañas y mosaico"), &form);
    addToggle(form, tr("Pestañas activadas"), "tabs.enabled");
    addToggle(form, tr("Mosaico activado"), "tiling.enabled");
    addChoice(form, tr("Modo de mosaico"), "tiling.mode",
              {QStringLiteral("master-stack"), QStringLiteral("horizontal"),
               QStringLiteral("vertical"), QStringLiteral("grid"),
               QStringLiteral("hybrid")});
    addNumber(form, tr("Proporción maestro"), "tiling.master-ratio", 0.20, 0.80);
    addInteger(form, tr("Columnas"), "tiling.columns", 1, 64);
    layout->addWidget(windows);

    auto *profileBox = groupBox(tr("Perfil"), &form);
    addChoice(form, tr("Perfil activo"), "profile",
              {QStringLiteral("custom"), QStringLiteral("minimal"),
               QStringLiteral("enhanced")});
    layout->addWidget(profileBox);

    auto *ipc = groupBox(tr("IPC de Stellar WiM"), &form);
    addToggle(form, tr("Socket IPC activo"), "ipc.enabled");
    form->addRow(new QLabel(tr("Los cambios se guardan en ~/.config/stellar-wim/rc.xml"), content));
    layout->addWidget(ipc);

    auto *actions = new QHBoxLayout;
    auto *reload = new QPushButton(tr("Recargar valores"), content);
    auto *keybindings = new QPushButton(tr("Atajos…"), content);
    auto *rules = new QPushButton(tr("Reglas de ventanas…"), content);
    auto *defaults = new QPushButton(tr("Restaurar valores predeterminados"), content);
    actions->addWidget(reload);
    actions->addWidget(keybindings);
    actions->addWidget(rules);
    actions->addWidget(defaults);
    actions->addStretch(1);
    layout->addLayout(actions);
    connect(reload, &QPushButton::clicked, this, &StellarWimConfigWidget::refresh);
    connect(keybindings, &QPushButton::clicked, this, &StellarWimConfigWidget::manageKeybindings);
    connect(rules, &QPushButton::clicked, this, &StellarWimConfigWidget::manageRules);
    connect(defaults, &QPushButton::clicked, this, &StellarWimConfigWidget::restoreDefaults);
}

QCheckBox *StellarWimConfigWidget::addToggle(QFormLayout *layout,
                                              const QString &label, const char *key)
{
    auto *box = new QCheckBox(label);
    layout->addRow(label, box);
    connect(box, &QCheckBox::toggled, this, [this, key, box](bool checked) {
        if (!m_loading) {
            writeValue(key, checked ? QStringLiteral("true") : QStringLiteral("false"));
        }
    });
    box->setProperty("stellarKey", QByteArray(key));
    return box;
}

QSpinBox *StellarWimConfigWidget::addInteger(QFormLayout *layout,
                                              const QString &label, const char *key,
                                              int minimum, int maximum)
{
    auto *spin = new QSpinBox;
    spin->setRange(minimum, maximum);
    layout->addRow(label, spin);
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, key, spin](int value) {
        if (!m_loading) {
            writeValue(key, QString::number(value));
        }
    });
    spin->setProperty("stellarKey", QByteArray(key));
    return spin;
}

QDoubleSpinBox *StellarWimConfigWidget::addNumber(QFormLayout *layout,
                                                   const QString &label,
                                                   const char *key,
                                                   double minimum, double maximum)
{
    auto *spin = new QDoubleSpinBox;
    spin->setRange(minimum, maximum);
    spin->setDecimals(2);
    spin->setSingleStep(0.05);
    layout->addRow(label, spin);
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, key, spin](double value) {
        if (!m_loading) {
            writeValue(key, QString::number(value, 'f', 2));
        }
    });
    spin->setProperty("stellarKey", QByteArray(key));
    return spin;
}

QComboBox *StellarWimConfigWidget::addChoice(QFormLayout *layout,
                                              const QString &label,
                                              const char *key,
                                              const QStringList &choices)
{
    auto *combo = new QComboBox;
    combo->addItems(choices);
    layout->addRow(label, combo);
    connect(combo, &QComboBox::currentTextChanged, this,
            [this, key, combo](const QString &value) {
        if (!m_loading) {
            writeValue(key, value);
        }
    });
    combo->setProperty("stellarKey", QByteArray(key));
    return combo;
}

void StellarWimConfigWidget::setControlValue(const char *key,
                                              const QJsonValue &value)
{
    const QByteArray id(key);
    for (QObject *child : findChildren<QObject *>()) {
        if (child->property("stellarKey").toByteArray() != id) {
            continue;
        }
        const QSignalBlocker blocker(child);
        if (auto *box = qobject_cast<QCheckBox *>(child)) {
            box->setChecked(value.toBool());
        } else if (auto *spin = qobject_cast<QSpinBox *>(child)) {
            spin->setValue(value.toInt(spin->value()));
        } else if (auto *spin = qobject_cast<QDoubleSpinBox *>(child)) {
            spin->setValue(value.toDouble(spin->value()));
        } else if (auto *combo = qobject_cast<QComboBox *>(child)) {
            const int index = combo->findText(value.toString());
            if (index >= 0) {
                combo->setCurrentIndex(index);
            }
        }
    }
    if (!qstrcmp(key, "theme.name") && m_themeName) {
        const QSignalBlocker blocker(m_themeName);
        m_themeName->setText(value.toString());
    }
}

void StellarWimConfigWidget::setStatus(const QString &message, bool error)
{
    if (!m_status) {
        return;
    }
    m_status->setText(message);
    m_status->setStyleSheet(error ? QStringLiteral("color: #ff8080;")
                                  : QStringLiteral("color: palette(mid-color);"));
}

void StellarWimConfigWidget::refresh()
{
    if (m_loading || m_writing) {
        return;
    }
    m_loading = true;
    m_client->request(QStringLiteral("CONFIG GET"),
                      [this](const StellarIpcClient::Result &result) {
        m_loading = false;
        if (!m_pendingValues.isEmpty()) {
            sendNextValue();
            return;
        }
        if (!result.ok || !result.value.isObject()) {
            setStatus(tr("Stellar WiM IPC no disponible: %1")
                          .arg(result.error.isEmpty()
                                   ? tr("el compositor no está activo")
                                   : result.error),
                      true);
            return;
        }
        m_values = result.value.toObject();
        for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
            setControlValue(it.key().toUtf8().constData(), it.value());
        }
        setStatus(tr("Conectado a Stellar WiM IPC. Los cambios se aplican y guardan de inmediato."));
    });
    m_client->request(QStringLiteral("CONFIG STATUS"),
                      [this](const StellarIpcClient::Result &result) {
        if (result.ok && result.value.isObject()) {
            const QString path = result.value.toObject()
                                     .value(QStringLiteral("config_file")).toString();
            if (!path.isEmpty()) {
                setStatus(tr("Configuración activa: %1").arg(path));
            }
        }
    });
}

void StellarWimConfigWidget::writeValue(const char *key, const QString &value)
{
    if (!qstrcmp(key, "ipc.enabled") && value == QLatin1String("false")) {
        const auto answer = QMessageBox::question(
            this, tr("Desactivar IPC"),
            tr("Esto cerrará el socket IPC y esta aplicación perderá la conexión. ¿Continuar?"));
        if (answer != QMessageBox::Yes) {
            refresh();
            return;
        }
    }
    m_pendingValues.insert(QString::fromLatin1(key), value);
    sendNextValue();
}

void StellarWimConfigWidget::sendNextValue()
{
    if (m_loading || m_writing || m_pendingValues.isEmpty()) {
        return;
    }
    auto it = m_pendingValues.begin();
    const QString key = it.key();
    const QString value = it.value();
    m_pendingValues.erase(it);
    m_writing = true;
    // An empty theme name is represented by removing the XML entry, not by
    // sending a command with a missing token.
    const QString command = value.isEmpty()
        ? QStringLiteral("CONFIG RESET %1").arg(key)
        : QStringLiteral("CONFIG SET %1 %2")
              .arg(key, value);
    m_client->request(command, [this, key](const StellarIpcClient::Result &result) {
        m_writing = false;
        if (!result.ok) {
            setStatus(tr("No se pudo aplicar %1: %2")
                          .arg(key, result.error), true);
        } else {
            setStatus(tr("%1 guardado").arg(key));
        }
        if (!m_pendingValues.isEmpty()) {
            sendNextValue();
        } else {
            refresh();
        }
    });
}

void StellarWimConfigWidget::manageKeybindings()
{
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Atajos de Stellar WiM"));
    auto *layout = new QVBoxLayout(dialog);
    auto *note = new QLabel(
        tr("Solo se permiten acciones del compositor; no se aceptan comandos de shell."),
        dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto *list = new QListWidget(dialog);
    layout->addWidget(list, 1);

    auto *form = new QFormLayout;
    auto *key = new QLineEdit(dialog);
    key->setPlaceholderText(QStringLiteral("W-F12"));
    auto *action = new QComboBox(dialog);
    action->addItems({
        QStringLiteral("StellarTilingToggle"), QStringLiteral("StellarTilingReapply"),
        QStringLiteral("StellarTabGroup"), QStringLiteral("StellarTabNext"),
        QStringLiteral("StellarTabPrevious"), QStringLiteral("StellarTabDetach"),
        QStringLiteral("StellarProfileMinimal"), QStringLiteral("StellarProfileEnhanced"),
        QStringLiteral("ToggleMaximize"), QStringLiteral("ToggleFullscreen"),
        QStringLiteral("ToggleDecorations"), QStringLiteral("ToggleShowDesktop"),
        QStringLiteral("NextWindow"), QStringLiteral("PreviousWindow"),
        QStringLiteral("Iconify"), QStringLiteral("Close"), QStringLiteral("None"),
    });
    form->addRow(tr("Atajo"), key);
    form->addRow(tr("Acción"), action);
    layout->addLayout(form);

    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(tr("Añadir o reemplazar"), dialog);
    auto *remove = new QPushButton(tr("Quitar"), dialog);
    auto *reset = new QPushButton(tr("Restablecer"), dialog);
    auto *close = new QPushButton(tr("Cerrar"), dialog);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addWidget(reset);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    auto refresh = [this, list = QPointer<QListWidget>(list)] {
        if (!list) return;
        m_client->request(QStringLiteral("CONFIG KEYBINDINGS LIST"),
                          [list](const StellarIpcClient::Result &result) {
            if (!list) return;
            list->clear();
            if (!result.ok || !result.value.isArray()) {
                return;
            }
            for (const QJsonValue &entry : result.value.toArray()) {
                const QJsonObject object = entry.toObject();
                QStringList actions;
                for (const QJsonValue &value :
                     object.value(QStringLiteral("actions")).toArray()) {
                    actions.append(value.toString());
                }
                const QString name =
                    object.value(QStringLiteral("key")).toString();
                auto *item = new QListWidgetItem(
                    QStringLiteral("%1 → %2").arg(name, actions.join(QStringLiteral(", "))));
                item->setData(Qt::UserRole, name);
                list->addItem(item);
            }
        });
    };

    connect(add, &QPushButton::clicked, this, [this, key, action, refresh] {
        const QString name = key->text().trimmed();
        if (name.isEmpty()) {
            return;
        }
        m_client->request(QStringLiteral("CONFIG KEYBINDINGS SET %1 %2")
                              .arg(name, action->currentText()),
                          [this, refresh](const StellarIpcClient::Result &result) {
            setStatus(result.ok ? tr("Atajo guardado") : tr("No se pudo guardar el atajo: %1").arg(result.error), !result.ok);
            refresh();
        });
    });
    connect(remove, &QPushButton::clicked, this, [this, list, refresh] {
        QListWidgetItem *item = list->currentItem();
        if (!item) {
            return;
        }
        m_client->request(QStringLiteral("CONFIG KEYBINDINGS REMOVE %1")
                              .arg(item->data(Qt::UserRole).toString()),
                          [this, refresh](const StellarIpcClient::Result &result) {
            setStatus(result.ok ? tr("Atajo eliminado") : tr("No se pudo eliminar el atajo: %1").arg(result.error), !result.ok);
            refresh();
        });
    });
    connect(reset, &QPushButton::clicked, this, [this, refresh] {
        m_client->request(QStringLiteral("CONFIG KEYBINDINGS RESET"),
                          [this, refresh](const StellarIpcClient::Result &result) {
            setStatus(result.ok ? tr("Atajos restablecidos") : tr("No se pudieron restablecer los atajos: %1").arg(result.error), !result.ok);
            refresh();
        });
    });
    connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
    refresh();
    dialog->exec();
    dialog->deleteLater();
}

void StellarWimConfigWidget::manageRules()
{
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Reglas de ventanas de Stellar WiM"));
    auto *layout = new QVBoxLayout(dialog);
    auto *note = new QLabel(
        tr("El identificador debe ser un app-id, WM_CLASS o patrón sin espacios."),
        dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto *list = new QListWidget(dialog);
    layout->addWidget(list, 1);

    auto *form = new QFormLayout;
    auto *identifier = new QLineEdit(dialog);
    identifier->setPlaceholderText(QStringLiteral("foot"));
    auto *property = new QComboBox(dialog);
    property->addItems({
        QStringLiteral("stellarTilingMode"), QStringLiteral("stellarTiling"),
        QStringLiteral("serverDecoration"), QStringLiteral("skipTaskbar"),
        QStringLiteral("skipWindowSwitcher"), QStringLiteral("ignoreFocusRequest"),
        QStringLiteral("ignoreConfigureRequest"), QStringLiteral("allowAlwaysOnTop"),
        QStringLiteral("fixedPosition"), QStringLiteral("iconPriority"),
    });
    auto *value = new QComboBox(dialog);
    value->setEditable(true);
    auto *properties = new QHBoxLayout;
    properties->addWidget(property, 1);
    properties->addWidget(value, 1);
    form->addRow(tr("Identificador"), identifier);
    form->addRow(tr("Propiedad / valor"), properties);
    layout->addLayout(form);

    auto choices = [value](const QString &name) {
        QStringList items;
        if (name == QLatin1String("stellarTilingMode")) {
            items = {QStringLiteral("master-stack"), QStringLiteral("horizontal"),
                     QStringLiteral("vertical"), QStringLiteral("grid"),
                     QStringLiteral("hybrid")};
        } else if (name == QLatin1String("stellarTiling")) {
            items = {QStringLiteral("floating"), QStringLiteral("managed")};
        } else if (name == QLatin1String("iconPriority")) {
            items = {QStringLiteral("server"), QStringLiteral("client")};
        } else {
            items = {QStringLiteral("yes"), QStringLiteral("no"),
                     QStringLiteral("default")};
        }
        value->clear();
        value->addItems(items);
    };
    choices(property->currentText());
    connect(property, &QComboBox::currentTextChanged, dialog, choices);

    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(tr("Añadir o actualizar"), dialog);
    auto *removeProperty = new QPushButton(tr("Quitar propiedad"), dialog);
    auto *removeRule = new QPushButton(tr("Quitar regla"), dialog);
    auto *reset = new QPushButton(tr("Restablecer"), dialog);
    auto *close = new QPushButton(tr("Cerrar"), dialog);
    buttons->addWidget(add);
    buttons->addWidget(removeProperty);
    buttons->addWidget(removeRule);
    buttons->addWidget(reset);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    auto refresh = [this, list = QPointer<QListWidget>(list)] {
        if (!list) return;
        m_client->request(QStringLiteral("CONFIG RULES LIST"),
                          [list](const StellarIpcClient::Result &result) {
            if (!list) return;
            list->clear();
            if (!result.ok || !result.value.isArray()) {
                return;
            }
            for (const QJsonValue &entry : result.value.toArray()) {
                const QJsonObject object = entry.toObject();
                QStringList parts;
                const QJsonObject attributes =
                    object.value(QStringLiteral("attributes")).toObject();
                for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
                    parts.append(QStringLiteral("%1=%2")
                                     .arg(it.key(), it.value().toString()));
                }
                const QString name =
                    attributes.value(QStringLiteral("identifier")).toString();
                auto *item = new QListWidgetItem(parts.join(QLatin1Char(' ')));
                item->setData(Qt::UserRole, name);
                list->addItem(item);
            }
        });
    };

    connect(add, &QPushButton::clicked, this, [this, identifier, property, value, refresh] {
        const QString id = identifier->text().trimmed();
        if (id.isEmpty()) {
            return;
        }
        m_client->request(QStringLiteral("CONFIG RULES SET %1 %2 %3")
                              .arg(id, property->currentText(), value->currentText()),
                          [this, refresh](const StellarIpcClient::Result &result) {
            setStatus(result.ok ? tr("Regla guardada") : tr("No se pudo guardar la regla: %1").arg(result.error), !result.ok);
            refresh();
        });
    });
    auto remove = [this, identifier, property, list, refresh](bool onlyProperty) {
        const QListWidgetItem *item = list->currentItem();
        const QString id = item ? item->data(Qt::UserRole).toString()
                                : identifier->text().trimmed();
        if (id.isEmpty()) {
            return;
        }
        QString command = QStringLiteral("CONFIG RULES REMOVE %1").arg(id);
        if (onlyProperty) {
            command += QLatin1Char(' ') + property->currentText();
        }
        m_client->request(command,
                          [this, refresh](const StellarIpcClient::Result &result) {
            setStatus(result.ok ? tr("Regla actualizada") : tr("No se pudo actualizar la regla: %1").arg(result.error), !result.ok);
            refresh();
        });
    };
    connect(removeProperty, &QPushButton::clicked, this, [remove] { remove(true); });
    connect(removeRule, &QPushButton::clicked, this, [remove] { remove(false); });
    connect(reset, &QPushButton::clicked, this, [this, refresh] {
        m_client->request(QStringLiteral("CONFIG RULES RESET"),
                          [this, refresh](const StellarIpcClient::Result &result) {
            setStatus(result.ok ? tr("Reglas restablecidas") : tr("No se pudieron restablecer las reglas: %1").arg(result.error), !result.ok);
            refresh();
        });
    });
    connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
    refresh();
    dialog->exec();
    dialog->deleteLater();
}

void StellarWimConfigWidget::restoreDefaults()
{
    const auto answer = QMessageBox::question(
        this, tr("Restaurar valores predeterminados"),
        tr("¿Restaurar todos los ajustes de Stellar WiM a sus valores originales?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_writing = true;
    m_client->request(QStringLiteral("CONFIG RESET SECTION ALL"),
                      [this](const StellarIpcClient::Result &result) {
        m_writing = false;
        if (!result.ok) {
            setStatus(tr("No se pudieron restaurar los valores: %1")
                          .arg(result.error), true);
        } else {
            setStatus(tr("Valores predeterminados restaurados y guardados"));
        }
        refresh();
    });
}
