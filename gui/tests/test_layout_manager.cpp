// gui/tests/test_layout_manager.cpp — LayoutManager save/load symmetry
// (design §3.11.2).
//
// Verifies that save() serializes the main-window geometry + dock state to a
// JSON file, and that load() restores the dock layout into a fresh window.
// Uses synthetic QMainWindow + QDockWidget instances and a temp file path.

#include <gtest/gtest.h>

#include <QApplication>
#include <QByteArray>
#include <QDockWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>

#include <filesystem>

#include "config/layout_manager.h"

using gui::LayoutManager;

namespace {

QString temp_path() {
    auto base = std::filesystem::temp_directory_path();
    base /= "gui_test_layout.json";
    return QString::fromStdString(base.string());
}

// A main window with two named docks — restoreState() requires stable
// objectNames so the serialized state can be matched back to widgets.
struct WindowWithDocks {
    QMainWindow window;
    QDockWidget dock1;
    QDockWidget dock2;

    WindowWithDocks() : dock1(QStringLiteral("dock1")), dock2(QStringLiteral("dock2")) {
        window.setObjectName(QStringLiteral("main"));
        dock1.setObjectName(QStringLiteral("dock1"));
        dock2.setObjectName(QStringLiteral("dock2"));
        window.addDockWidget(Qt::LeftDockWidgetArea, &dock1);
        window.addDockWidget(Qt::RightDockWidgetArea, &dock2);
        window.resize(640, 480);
    }
};

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(LayoutManager, SaveWritesGeometryAndState) {
    WindowWithDocks w;
    w.window.show();

    LayoutManager lm(&w.window);
    lm.capture_default();

    const QString path = temp_path();
    if (QFile::exists(path)) QFile::remove(path);

    ASSERT_TRUE(lm.save(path));
    ASSERT_TRUE(QFile::exists(path));

    const QByteArray geometry = w.window.saveGeometry();
    const QByteArray state = w.window.saveState();

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const auto doc = QJsonDocument::fromJson(f.readAll());
    ASSERT_TRUE(doc.isObject());
    const auto obj = doc.object();
    ASSERT_TRUE(obj.contains("geometry"));
    ASSERT_TRUE(obj.contains("state"));
    EXPECT_EQ(QByteArray::fromBase64(obj.value("geometry").toString().toLatin1()),
              geometry);
    EXPECT_EQ(QByteArray::fromBase64(obj.value("state").toString().toLatin1()),
              state);

    QFile::remove(path);
}

TEST(LayoutManager, LoadRestoresDockState) {
    const QString path = temp_path();

    {
        WindowWithDocks src;
        src.window.show();
        // Change dock state: hide dock2 so the saved layout differs from the
        // default (both-visible) configuration.
        src.dock2.hide();
        ASSERT_FALSE(src.dock2.isVisible());
        LayoutManager lm(&src.window);
        ASSERT_TRUE(lm.save(path));
    }

    // Restore into a fresh window with matching dock objectNames.
    // dock2 is visible by default before load.
    WindowWithDocks dst;
    dst.window.show();
    ASSERT_TRUE(dst.dock2.isVisible());

    LayoutManager lm(&dst.window);
    ASSERT_TRUE(lm.load(path));

    // After loading the saved state, dock2 visibility is restored to hidden.
    // (saveState()/restoreState() bytes are not deterministic across different
    // QMainWindow instances on the offscreen platform, so we verify a
    // functional property instead of comparing raw QByteArrays.)
    EXPECT_FALSE(dst.dock2.isVisible());

    QFile::remove(path);
}

TEST(LayoutManager, SaveWithNullWindowFails) {
    LayoutManager lm(nullptr);
    EXPECT_FALSE(lm.save(temp_path()));
}

TEST(LayoutManager, LoadNonexistentFileFails) {
    WindowWithDocks w;
    LayoutManager lm(&w.window);
    EXPECT_FALSE(lm.load(QStringLiteral("/no/such/layout_file.json")));
}

TEST(LayoutManager, ResetLayoutRestoresCapturedDefault) {
    WindowWithDocks w;
    w.window.show();
    LayoutManager lm(&w.window);
    lm.capture_default();

    // Both docks are visible in the captured default.
    ASSERT_TRUE(w.dock1.isVisible());

    // Mutate the layout: hide dock1.
    w.dock1.hide();
    EXPECT_FALSE(w.dock1.isVisible());

    // reset_layout() restores the captured default (dock1 visible again).
    // (saveState() bytes are not deterministic on the offscreen platform, so
    // we verify the functional restoration of dock visibility instead.)
    lm.reset_layout();
    EXPECT_TRUE(w.dock1.isVisible());
}
