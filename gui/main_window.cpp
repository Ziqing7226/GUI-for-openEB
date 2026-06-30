// gui/main_window.cpp

#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

#include <metavision/sdk/core/utils/colors.h>

#include "panels/biases_panel.h"
#include "panels/devices_panel.h"
#include "panels/display_panel.h"
#include "panels/esp_panel.h"
#include "panels/information_panel.h"
#include "panels/roi_panel.h"
#include "panels/statistics_panel.h"
#include "panels/trigger_panel.h"

namespace gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), camera_(nullptr), algo_bridge_() {
    setWindowTitle(tr("GUI for openEB"));
    resize(1280, 720);

    display_ = new EventDisplayWidget(this);
    setCentralWidget(display_);

    settings_ = new SettingsPanel(this);
    auto* dock = new QDockWidget(tr("Settings"), this);
    dock->setObjectName("SettingsDock");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setWidget(settings_);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->setMinimumWidth(320);

    build_menus();
    build_status_bar();
    wire_signals();

    // Populate the device list on startup.
    on_refresh_devices();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    camera_.disconnect();
    event->accept();
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void MainWindow::build_menus() {
    auto* mb = menuBar();

    // File
    auto* m_file = mb->addMenu(tr("&File"));
    m_file->addAction(tr("&Open File..."), this, &MainWindow::on_open_file,
                      QKeySequence::Open);
    a_save_cfg_ = m_file->addAction(tr("Save Camera Config..."), this, &MainWindow::on_save_config);
    a_load_cfg_ = m_file->addAction(tr("Load Camera Config..."), this, &MainWindow::on_load_config);
    a_save_cfg_->setEnabled(false);
    a_load_cfg_->setEnabled(false);
    m_file->addSeparator();
    a_save_biases_ = m_file->addAction(tr("Save Biases..."), this, &MainWindow::on_save_biases);
    a_load_biases_ = m_file->addAction(tr("Load Biases..."), this, &MainWindow::on_load_biases);
    a_save_biases_->setEnabled(false);
    a_load_biases_->setEnabled(false);
    m_file->addSeparator();
    m_file->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence::Quit);

    // View
    auto* m_view = mb->addMenu(tr("&View"));
    // Toggle the settings dock visibility.
    auto* dock_toggle = m_view->addAction(tr("Toggle Settings Panel"));
    dock_toggle->setCheckable(true);
    dock_toggle->setChecked(true);
    connect(dock_toggle, &QAction::toggled, this, [this](bool on) {
        auto* dock = findChild<QDockWidget*>("SettingsDock");
        if (dock) {
            dock->setVisible(on);
        }
    });
    m_view->addAction(tr("Reset Layout"), this, [this]() {
        auto* dock = findChild<QDockWidget*>("SettingsDock");
        if (dock) {
            dock->setFloating(false);
            addDockWidget(Qt::RightDockWidgetArea, dock);
            dock->show();
        }
    });
    m_view->addSeparator();
    m_view->addAction(tr("&Fullscreen"), this,
                      [] { /* F11 fullscreen handled via shortcut */ },
                      QKeySequence("F11"));

    // Camera
    auto* m_cam = mb->addMenu(tr("&Camera"));
    m_cam->addAction(tr("&Connect First Available"), this,
                     &MainWindow::on_connect_first, QKeySequence("Ctrl+C"));
    m_cam->addAction(tr("&Disconnect"), this, &MainWindow::on_disconnect);
    m_cam->addSeparator();
    m_cam->addAction(tr("Refresh Device &List"), this, &MainWindow::on_refresh_devices);
    m_cam->addAction(tr("&Device List..."), this, [this]() {
        // The Devices panel in the settings dock already shows the list.
        auto* dock = findChild<QDockWidget*>("SettingsDock");
        if (dock) {
            dock->show();
            dock->raise();
        }
    });
    m_cam->addSeparator();
    a_roi_drag_ = m_cam->addAction(tr("&ROI Drag Mode"), this, &MainWindow::on_toggle_roi_drag);
    a_roi_drag_->setCheckable(true);
    a_roi_drag_->setChecked(false);
    a_roi_drag_->setShortcut(QKeySequence("Ctrl+R"));
    a_roi_drag_->setEnabled(false);

    // Preprocess (stub — Phase 5)
    auto* m_prep = mb->addMenu(tr("&Preprocess"));
    m_prep->setEnabled(false);
    m_prep->setTitle(tr("&Preprocess (Phase 5)"));

    // Frame Mode (stub — Phase 5)
    auto* m_frame = mb->addMenu(tr("Frame &Mode"));
    m_frame->setEnabled(false);
    m_frame->setTitle(tr("Frame &Mode (Phase 5)"));

    // Algorithm (stub — Phases 6-8)
    auto* m_algo = mb->addMenu(tr("&Algorithm"));
    m_algo->setEnabled(false);
    m_algo->setTitle(tr("&Algorithm (Phases 6-8)"));

    // Calibration (stub — Phase 9)
    auto* m_calib = mb->addMenu(tr("&Calibration"));
    m_calib->setEnabled(false);
    m_calib->setTitle(tr("&Calibration (Phase 9)"));

    // Tools (stub — Phase 10)
    auto* m_tools = mb->addMenu(tr("&Tools"));
    m_tools->setEnabled(false);
    m_tools->setTitle(tr("&Tools (Phase 10)"));

    // Help
    auto* m_help = mb->addMenu(tr("&Help"));
    m_help->addAction(tr("&About"), this, &MainWindow::on_about);
    m_help->addAction(tr("About &Qt"), this, &QApplication::aboutQt);
}

void MainWindow::build_status_bar() {
    auto* sb = statusBar();
    status_conn_ = new QLabel(tr("Disconnected"), this);
    status_rate_ = new QLabel(tr("— ev/s"), this);
    status_ts_   = new QLabel(tr("t: —"), this);
    status_rec_  = new QLabel(tr("Idle"), this);
    sb->addWidget(status_conn_);
    sb->addWidget(status_rate_);
    sb->addWidget(status_ts_);
    sb->addPermanentWidget(status_rec_);
}

void MainWindow::wire_signals() {
    // Devices panel <-> controller
    auto* dp = settings_->devices_panel();
    connect(dp, &DevicesPanel::refresh_requested, this, &MainWindow::on_refresh_devices);
    connect(dp, &DevicesPanel::connect_first_requested, this, &MainWindow::on_connect_first);
    connect(dp, &DevicesPanel::connect_serial_requested, this,
            [this](const QString& serial) { camera_.connect_serial(serial.toStdString()); });
    connect(dp, &DevicesPanel::disconnect_requested, this, &MainWindow::on_disconnect);

    // Controller -> UI
    connect(&camera_, &CameraController::connected, this, [this](const SensorInfo& info) {
        settings_->information_panel()->set_info(info);
        settings_->devices_panel()->set_connected(true);
        status_conn_->setText(tr("Connected: %1").arg(info.generation_name.isEmpty()
                                                           ? info.integrator
                                                           : info.generation_name));
        // Phase 2 panels: populate from the now-connected camera.
        settings_->biases_panel()->on_camera_connected(&camera_);
        settings_->roi_panel()->on_camera_connected(&camera_);
        settings_->esp_panel()->on_camera_connected(&camera_);
        settings_->trigger_panel()->on_camera_connected(&camera_);
        // Enable config / bias file actions on live cameras only.
        const bool live = !info.is_file;
        a_save_cfg_->setEnabled(live);
        a_load_cfg_->setEnabled(live);
        a_save_biases_->setEnabled(live);
        a_load_biases_->setEnabled(live);
        a_roi_drag_->setEnabled(true);
        camera_.start();
    });
    connect(&camera_, &CameraController::disconnected, this, [this]() {
        settings_->information_panel()->clear();
        settings_->statistics_panel()->clear();
        settings_->devices_panel()->set_connected(false);
        settings_->biases_panel()->on_camera_disconnected();
        settings_->roi_panel()->on_camera_disconnected();
        settings_->esp_panel()->on_camera_disconnected();
        settings_->trigger_panel()->on_camera_disconnected();
        status_conn_->setText(tr("Disconnected"));
        status_rate_->setText(tr("— ev/s"));
        status_ts_->setText(tr("t: —"));
        a_save_cfg_->setEnabled(false);
        a_load_cfg_->setEnabled(false);
        a_save_biases_->setEnabled(false);
        a_load_biases_->setEnabled(false);
        a_roi_drag_->setEnabled(false);
        a_roi_drag_->setChecked(false);
        display_->set_roi_drag_mode(false);
        display_->clear();
    });
    connect(&camera_, &CameraController::started, this, [this]() {
        status_rec_->setText(tr("Streaming"));
    });
    connect(&camera_, &CameraController::stopped, this, [this]() {
        status_rec_->setText(tr("Idle"));
    });
    connect(&camera_, &CameraController::error, this, [this](const QString& msg) {
        QMessageBox::warning(this, tr("Camera error"), msg);
    });
    connect(&camera_, &CameraController::runtime_warning, this, [this](const QString& msg) {
        status_rec_->setText(tr("Stopped"));
        statusBar()->showMessage(msg, 5000);
    });

    // Frame pipeline -> display
    connect(camera_.frame_pipeline(), &FramePipeline::frame_ready, display_,
            [this](QImage frame, Metavision::timestamp ts) {
                display_->set_frame(frame);
                settings_->statistics_panel()->set_timestamp(ts);
                status_ts_->setText(QStringLiteral("t: %1 s").arg(ts / 1.0e6, 0, 'f', 3));
            });

    // Statistics controller -> panel + status bar
    connect(camera_.statistics(), &StatisticsController::rate_updated, this,
            [this](double rate, double peak, Metavision::timestamp t) {
                settings_->statistics_panel()->set_rate(rate, peak, t);
                if (rate >= 1.0e6) {
                    status_rate_->setText(QStringLiteral("%1 Mev/s")
                                              .arg(rate / 1.0e6, 0, 'f', 2));
                } else if (rate >= 1.0e3) {
                    status_rate_->setText(QStringLiteral("%1 kev/s")
                                              .arg(rate / 1.0e3, 0, 'f', 2));
                } else {
                    status_rate_->setText(QStringLiteral("%1 ev/s").arg(rate, 0, 'f', 0));
                }
            });
    connect(camera_.statistics(), &StatisticsController::on_off_updated,
            settings_->statistics_panel(), &StatisticsPanel::set_on_off);

    // Display panel -> pipeline
    connect(settings_->display_panel(), &DisplayPanel::color_palette_changed, this,
            &MainWindow::update_palettes);

    // ROI panel <-> display widget (Phase 2)
    auto* roi = settings_->roi_panel();
    connect(display_, &EventDisplayWidget::roi_dragged,
            roi, &RoiPanel::set_roi_from_drag);
    connect(roi, &RoiPanel::roi_applied, display_, &EventDisplayWidget::set_roi_overlay);

    // Panel info/error messages -> status bar / message box.
    auto forward = [this](const QString& msg, bool isError) {
        forward_panel_message(msg, isError);
    };
    connect(settings_->biases_panel(),  &BiasesPanel::info_message,  this,
            [forward](const QString& m) { forward(m, false); });
    connect(settings_->biases_panel(),  &BiasesPanel::error_message, this,
            [forward](const QString& m) { forward(m, true); });
    connect(roi, &RoiPanel::info_message,  this,
            [forward](const QString& m) { forward(m, false); });
    connect(roi, &RoiPanel::error_message, this,
            [forward](const QString& m) { forward(m, true); });
    connect(settings_->esp_panel(),     &EspPanel::info_message,  this,
            [forward](const QString& m) { forward(m, false); });
    connect(settings_->esp_panel(),     &EspPanel::error_message, this,
            [forward](const QString& m) { forward(m, true); });
    connect(settings_->trigger_panel(), &TriggerPanel::info_message,  this,
            [forward](const QString& m) { forward(m, false); });
    connect(settings_->trigger_panel(), &TriggerPanel::error_message, this,
            [forward](const QString& m) { forward(m, true); });
}

void MainWindow::update_palettes(int index) {
    Metavision::ColorPalette p = Metavision::ColorPalette::Dark;
    switch (index) {
        case 1: p = Metavision::ColorPalette::Light; break;
        case 2: p = Metavision::ColorPalette::CoolWarm; break;
        case 3: p = Metavision::ColorPalette::Gray; break;
        default: break;
    }
    camera_.frame_pipeline()->set_color_palette(p);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void MainWindow::on_open_file() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open event file"), QString(),
        tr("Event files (*.raw *.hdf5 *.h5 *.dat);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (!camera_.connect_file(path.toStdString())) {
        QMessageBox::warning(this, tr("Open file"),
                             tr("Failed to open event file:\n%1").arg(path));
        return;
    }
    camera_.start();
}

void MainWindow::on_connect_first() {
    if (!camera_.connect_first_available()) {
        QMessageBox::information(this, tr("Connect"),
                                 tr("No live camera available."));
    }
}

void MainWindow::on_disconnect() {
    camera_.disconnect();
}

void MainWindow::on_save_config() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save camera config"), QString(),
        tr("Camera config (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    if (!camera_.save_config(path.toStdString())) {
        QMessageBox::warning(this, tr("Save config"),
                             tr("Failed to save camera config to:\n%1").arg(path));
    } else {
        statusBar()->showMessage(tr("Config saved: %1").arg(path), 4000);
    }
}

void MainWindow::on_load_config() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load camera config"), QString(),
        tr("Camera config (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    if (!camera_.load_config(path.toStdString())) {
        QMessageBox::warning(this, tr("Load config"),
                             tr("Failed to load camera config from:\n%1").arg(path));
        return;
    }
    // Refresh the panels so they reflect the newly-applied state.
    settings_->biases_panel()->on_camera_connected(&camera_);
    settings_->roi_panel()->on_camera_connected(&camera_);
    settings_->esp_panel()->on_camera_connected(&camera_);
    settings_->trigger_panel()->on_camera_connected(&camera_);
    statusBar()->showMessage(tr("Config loaded: %1").arg(path), 4000);
}

void MainWindow::on_save_biases() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save biases"), QString(),
        tr("Bias files (*.bias);;All files (*)"));
    if (path.isEmpty()) return;
    settings_->biases_panel()->save_to_file(path);
}

void MainWindow::on_load_biases() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load biases"), QString(),
        tr("Bias files (*.bias);;All files (*)"));
    if (path.isEmpty()) return;
    settings_->biases_panel()->load_from_file(path);
}

void MainWindow::on_toggle_roi_drag(bool on) {
    display_->set_roi_drag_mode(on);
    statusBar()->showMessage(on ? tr("ROI drag mode on — draw a rectangle on the display.")
                                : tr("ROI drag mode off."), 3000);
}

void MainWindow::forward_panel_message(const QString& msg, bool isError) {
    if (isError) {
        QMessageBox::warning(this, tr("Camera control"), msg);
    } else {
        statusBar()->showMessage(msg, 4000);
    }
}

void MainWindow::on_refresh_devices() {
    const auto sources = camera_.list_online_sources();
    settings_->devices_panel()->refresh_sources(sources);
    if (sources.empty()) {
        statusBar()->showMessage(tr("No cameras detected."), 3000);
    } else {
        statusBar()->showMessage(tr("%1 camera(s) found.").arg(sources.size()), 3000);
    }
}

void MainWindow::on_about() {
    QMessageBox::about(this, tr("About GUI for openEB"),
        tr("<h3>GUI for openEB</h3>"
           "<p>Phase 2 — Qt 6 + OpenEB 5.2.0.</p>"
           "<p>Camera discovery, real-time OpenGL event display, statistics, "
           "Bias / ROI / ESP (Anti-Flicker · Trail Filter · ERC) / Trigger "
           "control panels, ROI drag-draw overlay, and JSON config save/load.</p>"
           "<p>See doc/design.md for the full roadmap.</p>"));
}

} // namespace gui
