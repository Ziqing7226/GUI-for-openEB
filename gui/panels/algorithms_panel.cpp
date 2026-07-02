// gui/panels/algorithms_panel.cpp

#include "algorithms_panel.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMap>

#include "algo_bridge/algo_bridge.h"

namespace gui {

AlgorithmsPanel::AlgorithmsPanel(AlgoBridge* bridge, QWidget* parent)
    : QWidget(parent), bridge_(bridge) {
    build_ui();
}

void AlgorithmsPanel::build_ui() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto* host = new QWidget(scroll);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    if (!bridge_) {
        auto* lbl = new QLabel(tr("Algorithm bridge unavailable."), host);
        layout->addWidget(lbl);
        layout->addStretch(1);
        scroll->setWidget(host);
        return;
    }

    // Group algorithms by category. Only self-developed algorithms are shown
    // here: OpenEB-wrapped filters have no real backend in AlgoBridge and are
    // controlled via the Preprocess menu / PreprocessingPanel instead.
    algos_ = bridge_->list_algos();
    QMap<QString, std::vector<const AlgoInfo*>> by_cat;
    for (const auto& a : algos_) {
        if (a.source != "self") continue;
        by_cat[QString::fromStdString(a.category)].push_back(&a);
    }

    static const QMap<QString, QString> cat_titles = {
        {"cv",              tr("CV Algorithms (Phase 6-7)")},
        {"analytics",       tr("Analytics (Phase 8)")},
        {"calibration",     tr("Calibration (Phase 9)")},
    };

    for (auto it = by_cat.constBegin(); it != by_cat.constEnd(); ++it) {
        const QString title = cat_titles.value(it.key(), it.key());
        auto* gb = new QGroupBox(title, host);
        auto* form = new QFormLayout(gb);
        form->setContentsMargins(6, 6, 6, 6);

        for (const auto* a : it.value()) {
            // OpenEB-wrapped algorithms (source != "self") have no real
            // backend in AlgoBridge — their parameters don't take effect here.
            // They are controlled via the Preprocess menu / PreprocessingPanel
            // FilterChain. Skip them to avoid presenting dead controls.
            if (a->source != "self") continue;

            auto* cb = new QCheckBox(QString::fromStdString(a->display_name), gb);
            checkboxes_[a->name] = cb;
            form->addRow(cb);

            // Parameter editor (shown only when enabled).
            auto* params_host = new QWidget(gb);
            auto* pform = new QFormLayout(params_host);
            pform->setContentsMargins(20, 0, 0, 0);
            params_host->setVisible(false);

            const std::string algo_name = a->name;
            // Match a default value to an enum_values entry. Entries may be
            // "N=Label" (match on the "N" prefix) or plain values.
            auto match_enum_index = [](const std::vector<std::string>& vals,
                                       const std::string& def) -> int {
                for (size_t i = 0; i < vals.size(); ++i) {
                    const auto& v = vals[i];
                    const auto eq = v.find('=');
                    const std::string token = (eq == std::string::npos)
                        ? v : v.substr(0, eq);
                    if (token == def) return static_cast<int>(i);
                }
                return -1;
            };
            for (const auto& p : a->params) {
                const QString disp = QString::fromStdString(p.display_name);
                const std::string param_key = p.key;
                QWidget* w = nullptr;
                if (p.type == "enum") {
                    auto* cmb = new QComboBox(params_host);
                    for (const auto& v : p.enum_values) cmb->addItem(QString::fromStdString(v));
                    const int idx = match_enum_index(p.enum_values, p.default_value);
                    if (idx >= 0) cmb->setCurrentIndex(idx);
                    w = cmb;
                    connect(cmb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                            [this, algo_name, param_key, cmb](int) {
                                apply_param(algo_name, param_key, cmb->currentText().toStdString());
                            });
                } else if (p.type == "bool") {
                    auto* cmb = new QComboBox(params_host);
                    cmb->addItem("false"); cmb->addItem("true");
                    cmb->setCurrentIndex(p.default_value == "true" || p.default_value == "1" ? 1 : 0);
                    w = cmb;
                    connect(cmb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                            [this, algo_name, param_key, cmb](int) {
                                apply_param(algo_name, param_key, cmb->currentText().toStdString());
                            });
                } else if (p.type == "int") {
                    auto* sp = new QSpinBox(params_host);
                    bool oklo = false, okhi = false;
                    int lo = QString::fromStdString(p.min_value).toInt(&oklo);
                    int hi = QString::fromStdString(p.max_value).toInt(&okhi);
                    sp->setRange(oklo ? lo : -100000000, okhi ? hi : 100000000);
                    sp->setValue(QString::fromStdString(p.default_value).toInt());
                    w = sp;
                    connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this,
                            [this, algo_name, param_key](int v) {
                                apply_param(algo_name, param_key, std::to_string(v));
                            });
                } else if (p.type == "float") {
                    auto* sp = new QDoubleSpinBox(params_host);
                    sp->setRange(-1e9, 1e9);
                    sp->setDecimals(6);
                    sp->setValue(QString::fromStdString(p.default_value).toDouble());
                    w = sp;
                    connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                            [this, algo_name, param_key](double v) {
                                apply_param(algo_name, param_key, std::to_string(v));
                            });
                } else {
                    auto* le = new QLineEdit(QString::fromStdString(p.default_value), params_host);
                    w = le;
                    connect(le, &QLineEdit::textChanged, this,
                            [this, algo_name, param_key](const QString& v) {
                                apply_param(algo_name, param_key, v.toStdString());
                            });
                }
                pform->addRow(disp, w);
            }
            form->addRow(QString(), params_host);

            connect(cb, &QCheckBox::toggled, this, [this, params_host, cb, a, algo_name](bool on) {
                params_host->setVisible(on);
                if (on) {
                    // Reuse the live instance if one already exists (e.g. the
                    // user edited a parameter before enabling). create() would
                    // discard those parameters by building a fresh instance.
                    auto inst = bridge_->find_or_create(algo_name);
                    if (inst) {
                        inst->set_enabled(true);
                        live_instances_[algo_name] = inst;
                    }
                    emit info_message(tr("Algorithm enabled: %1")
                                          .arg(QString::fromStdString(a->display_name)));
                } else {
                    auto it = live_instances_.find(algo_name);
                    if (it != live_instances_.end() && it->second) {
                        it->second->set_enabled(false);
                    }
                }
                emit algorithm_toggled(QString::fromStdString(a->name), on);
            });
        }
        layout->addWidget(gb);
    }

    layout->addStretch(1);
    scroll->setWidget(host);
}

void AlgorithmsPanel::apply_param(const std::string& algo_name,
                                  const std::string& param_key,
                                  const std::string& value) {
    // Lazily create the instance so parameter edits are recorded even before
    // the enable checkbox is toggled. find_or_create preserves any previously
    // set parameters instead of overwriting them with defaults.
    auto it = live_instances_.find(algo_name);
    if (it == live_instances_.end() || !it->second) {
        auto inst = bridge_ ? bridge_->find_or_create(algo_name) : nullptr;
        if (!inst) return;
        it = live_instances_.emplace(algo_name, inst).first;
    }
    it->second->set_param(param_key, value);
}

void AlgorithmsPanel::set_algo_enabled(const std::string& name, bool on) {
    auto it = checkboxes_.find(name);
    if (it == checkboxes_.end() || !it->second) return;
    // Block signals so this programmatic change does not re-enter the toggled
    // handler (which would create/enable instances and emit algorithm_toggled,
    // causing sync loops with the Algorithm menu / AlgoWindow).
    QSignalBlocker b(it->second);
    it->second->setChecked(on);
}

} // namespace gui
