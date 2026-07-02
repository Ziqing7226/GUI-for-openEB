// gui/widgets/multi_window_manager.cpp

#include "multi_window_manager.h"

#include <QMdiSubWindow>

#include "display/event_display_widget.h"

namespace gui {

MultiWindowManager::MultiWindowManager(QMainWindow* parent)
    : QObject(parent), main_window_(parent) {
    // The QMdiArea lives inside a fresh top-level QMainWindow so that the
    // spawned sub-windows are actually visible. Parenting the area to the
    // main window without placing it in a layout would leave it invisible
    // and "Add Display Window" would be a no-op.
    // setWindowFlag(Qt::Window) is required so the QMainWindow becomes a
    // real top-level window instead of an embedded child widget.
    host_window_ = new QMainWindow(parent ? parent->window() : nullptr);
    host_window_->setWindowFlag(Qt::Window, true);
    host_window_->setAttribute(Qt::WA_DeleteOnClose, false);
    host_window_->setWindowTitle(tr("Display Windows"));
    mdi_area_ = new QMdiArea(host_window_);
    mdi_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mdi_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mdi_area_->setActivationOrder(QMdiArea::CreationOrder);
    host_window_->setCentralWidget(mdi_area_);
    host_window_->resize(900, 600);
}

MultiWindowManager::~MultiWindowManager() {
    if (host_window_) {
        host_window_->close();
        delete host_window_;
    }
}

EventDisplayWidget* MultiWindowManager::add_display(const QString& title) {
    if (!mdi_area_) return nullptr;
    auto* sub = new QMdiSubWindow(mdi_area_);
    auto* display = new EventDisplayWidget(sub);
    sub->setWidget(display);
    sub->setWindowTitle(title);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    mdi_area_->addSubWindow(sub);
    sub->show();

    sub_windows_.push_back(sub);
    connect(sub, &QObject::destroyed, this, [this, sub]() {
        sub_windows_.erase(
            std::remove(sub_windows_.begin(), sub_windows_.end(), sub),
            sub_windows_.end());
        // Auto-hide the host window when the last sub-window is closed.
        if (sub_windows_.empty() && host_window_) {
            host_window_->hide();
        }
    });
    emit window_added(display);

    if (sub_windows_.size() > 1) tile();
    // Make sure the host is visible (it stays hidden after the first close).
    if (host_window_ && !host_window_->isVisible()) {
        host_window_->show();
    }
    host_window_->raise();
    host_window_->activateWindow();
    return display;
}

void MultiWindowManager::tile() {
    if (mdi_area_) mdi_area_->tileSubWindows();
}

void MultiWindowManager::cascade() {
    if (mdi_area_) mdi_area_->cascadeSubWindows();
}

void MultiWindowManager::close_all() {
    if (mdi_area_) mdi_area_->closeAllSubWindows();
    // closeAllSubWindows() posts close events that are processed in the next
    // event-loop iteration. The destroyed() lambdas (connected in
    // add_display) remove each entry from sub_windows_ as it is destroyed.
    // We only need to hide the host window; if no sub-windows were open,
    // hide immediately.
    if (sub_windows_.empty()) {
        if (host_window_) host_window_->hide();
    }
}

} // namespace gui
