// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp launcher — see docs/PLAN.md.

#include "config.hpp"
#include <install.hpp>
#include <iso9660.hpp>
#include <mpq.hpp>
#include <userdir.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QProgressBar>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static std::string mpq_target(std::string_view basename) {
    std::string upper(basename);
    for (auto& letter : upper) letter = static_cast<char>(std::toupper(letter));
    if (upper.size() > 4 && upper.starts_with("D2") &&
        upper.ends_with(".MPQ")) {
        std::string lower(basename);
        for (auto& letter : lower) letter = static_cast<char>(std::tolower(letter));
        return lower;
    }
    return {};
}

static std::string runtime_target(std::string_view basename) {
    std::string upper(basename);
    for (auto& letter : upper) letter = static_cast<char>(std::toupper(letter));
    static constexpr std::string_view kBins[] = {
        "GAME.EXE", "DIABLO II.EXE",
        "D2CLIENT.DLL", "D2COMMON.DLL", "D2GAME.DLL", "D2GFX.DLL",
        "D2LANG.DLL",   "D2LAUNCH.DLL", "D2MCPCLIENT.DLL",
        "D2MULTI.DLL",  "D2NET.DLL",    "D2SERVER.DLL",
        "D2SOUND.DLL",  "D2WIN.DLL",
        "BNCLIENT.DLL", "FOG.DLL",      "STORM.DLL",  "IJL11.DLL",
        "BINKW32.DLL",  "SMACKW32.DLL",
    };
    for (auto binary : kBins) if (upper == binary) {
        std::string lower(basename);
        for (auto& letter : lower) letter = static_cast<char>(std::tolower(letter));
        return "bin/" + lower;
    }
    return {};
}

static std::string dest_target(std::string_view basename) {
    if (auto target = mpq_target(basename);     !target.empty()) return target;
    if (auto target = runtime_target(basename); !target.empty()) return target;
    return {};
}

static std::string path_basename(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool is_iso_path(const QString& path) {
    return QFileInfo(path).suffix().compare("iso", Qt::CaseInsensitive) == 0;
}

static QString default_dest() {
    auto base = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) base = QDir::homePath();
    return base + "/game";
}

static std::filesystem::path to_path(const QString& text) {
    return std::filesystem::path(text.toStdU16String());
}

static QString from_path(const std::filesystem::path& path) {
    return QString::fromStdU16String(path.u16string());
}

// The install at `dir`, if it holds classic D2 or D2R (install::classify).
static std::optional<d2d::install::Install> classify_dir(const QString& dir, const std::string& source = "manual") {
    if (dir.isEmpty()) return std::nullopt;
    return d2d::install::classify(to_path(dir), source);
}

// What Browse was pointed at, as the folder to classify: a folder,
// Game.exe, or "Diablo II.app" (MPQs inside the bundle or beside it).
static QString normalize_pick(const QString& picked) {
    const QFileInfo info(picked);
    if (info.suffix().compare("app", Qt::CaseInsensitive) == 0) {
        const QString resources = info.absoluteFilePath() + "/Contents/Resources";
        return classify_dir(resources) ? resources : info.absolutePath();
    }
    return info.isFile() ? info.absolutePath() : info.absoluteFilePath();
}

static QFileDialog::Options macos_dlg_opts() {
#ifdef Q_OS_MACOS
    return QFileDialog::DontUseNativeDialog;
#else
    return QFileDialog::Options{};
#endif
}

// ===========================================================================
// Wizard pages
// ===========================================================================

// ---------------------------------------------------------------------------
// Page 1: sources (ISOs and/or folders).
// ---------------------------------------------------------------------------
class SourcePage : public QWizardPage {
public:
    SourcePage() {
        setTitle(tr("Where is your Diablo II?"));
        setSubTitle(tr(
            "Add the four CD ISO files, a folder holding those ISOs, or an "
            "existing D2 install (GOG, Battle.net Classic, prior install). "
            "Any mix works. Every D2*.MPQ found gets installed."));

        list_ = new QListWidget(this);
        auto* add_isos = new QPushButton(tr("Add ISO files..."), this);
        auto* add_dir  = new QPushButton(tr("Add folder..."), this);
        auto* remove   = new QPushButton(tr("Remove selected"), this);

        connect(add_isos, &QPushButton::clicked, this, [this] {
            QFileDialog dlg(this, tr("Select ISO files"), last_dir(),
                            tr("ISO images (*.iso *.ISO);;All files (*)"));
            dlg.setFileMode(QFileDialog::ExistingFiles);
            dlg.setOptions(macos_dlg_opts());
            if (dlg.exec() != QDialog::Accepted) return;
            for (const auto& file : dlg.selectedFiles()) {
                add_unique(file);
                remember_dir(QFileInfo(file).absolutePath());
            }
        });
        connect(add_dir, &QPushButton::clicked, this, [this] {
            QFileDialog dlg(this, tr("Select a folder (D2 install or ISOs)"),
                            last_dir());
            dlg.setFileMode(QFileDialog::Directory);
            dlg.setOption(QFileDialog::ShowDirsOnly, true);
            dlg.setOptions(macos_dlg_opts() | dlg.options());
            if (dlg.exec() != QDialog::Accepted) return;
            const auto sel = dlg.selectedFiles();
            if (sel.isEmpty()) return;
            const QString directory = sel.first();
            remember_dir(directory);
            const QDir dir(directory);
            const auto isos = dir.entryList({"*.iso", "*.ISO"},
                                            QDir::Files, QDir::Name);
            if (!isos.isEmpty()) {
                for (const auto& file : isos) add_unique(dir.absoluteFilePath(file));
            } else {
                add_unique(directory);
            }
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            qDeleteAll(list_->selectedItems());
            emit completeChanged();
        });

        auto* layout = new QVBoxLayout(this);
        layout->addWidget(list_);
        auto* row = new QHBoxLayout;
        row->addWidget(add_isos);
        row->addWidget(add_dir);
        row->addStretch();
        row->addWidget(remove);
        layout->addLayout(row);

        registerField("sources", this, "sourcePaths", SIGNAL(completeChanged()));
    }

    bool isComplete() const override { return list_->count() > 0; }

    Q_PROPERTY(QStringList sourcePaths READ sourcePaths)
    QStringList sourcePaths() const {
        QStringList out;
        for (int i = 0; i < list_->count(); ++i) out << list_->item(i)->text();
        return out;
    }

private:
    QString last_dir() const {
        return last_dir_.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            : last_dir_;
    }
    void remember_dir(const QString& directory) { last_dir_ = directory; }
    void add_unique(const QString& path) {
        if (list_->findItems(path, Qt::MatchExactly).isEmpty()) list_->addItem(path);
        emit completeChanged();
    }
    QListWidget* list_{};
    QString last_dir_;
    Q_OBJECT
};

// ---------------------------------------------------------------------------
// Page 2: destination.
// ---------------------------------------------------------------------------
class DestPage : public QWizardPage {
public:
    explicit DestPage(QString initial) {
        setTitle(tr("Choose install location"));
        setSubTitle(tr("MPQs will be written here. About 2 GB free needed "
                       "for a full install. Re-running is safe — MPQs "
                       "already present are skipped."));
        edit_ = new QLineEdit(this);
        edit_->setText(initial.isEmpty() ? default_dest() : initial);
        auto* browse = new QPushButton(tr("Browse..."), this);
        connect(browse, &QPushButton::clicked, this, [this] {
            QFileDialog dlg(this, tr("Destination"), edit_->text());
            dlg.setFileMode(QFileDialog::Directory);
            dlg.setOption(QFileDialog::ShowDirsOnly, true);
            dlg.setOptions(macos_dlg_opts() | dlg.options());
            if (dlg.exec() != QDialog::Accepted) return;
            const auto sel = dlg.selectedFiles();
            if (sel.isEmpty()) return;
            edit_->setText(sel.first());
            emit completeChanged();
        });
        connect(edit_, &QLineEdit::textChanged, this,
                &QWizardPage::completeChanged);

        auto* layout = new QVBoxLayout(this);
        auto* row = new QHBoxLayout;
        row->addWidget(edit_);
        row->addWidget(browse);
        layout->addLayout(row);
        layout->addStretch();

        registerField("dest*", edit_);
    }
    bool isComplete() const override { return !edit_->text().trimmed().isEmpty(); }
private:
    QLineEdit* edit_{};
    Q_OBJECT
};

// ---------------------------------------------------------------------------
// Page 3: install. Skips MPQs already at dest with matching size.
// ---------------------------------------------------------------------------
class CopyPage : public QWizardPage {
public:
    CopyPage() {
        setTitle(tr("Installing"));
        setSubTitle(tr("Copying D2 MPQ files into the destination."));
        bar_ = new QProgressBar(this);
        bar_->setRange(0, 100);
        status_ = new QLabel(tr("Preparing..."), this);
        status_->setWordWrap(true);
        summary_ = new QLabel(this);
        summary_->setWordWrap(true);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(bar_);
        layout->addWidget(status_);
        layout->addWidget(summary_);
        layout->addStretch();
    }

    void initializePage() override {
        done_.store(false);
        emit completeChanged();
        const auto sources = field("sources").toStringList();
        const auto dest = field("dest").toString();
        QThreadPool::globalInstance()->start(
            [this, sources, dest] { this->do_install(sources, dest); });
    }
    bool isComplete() const override { return done_.load(); }

private:
    struct Job {
        std::string out_name;
        std::uint64_t size{};
        std::shared_ptr<iso9660::Reader> reader;
        iso9660::Entry entry;
        std::filesystem::path local;
    };

    void do_install(const QStringList& sources, const QString& dest_str) {
        namespace fs = std::filesystem;
        const fs::path dest(dest_str.toStdString());
        std::error_code error;
        fs::create_directories(dest, error);

        std::vector<Job> jobs;
        for (const auto& source : sources) {
            const fs::path path(source.toStdString());
            if (is_iso_path(source))                collect_from_iso(path, jobs);
            else if (fs::is_directory(path, error))  collect_from_dir(path, jobs);
            else                               collect_from_iso(path, jobs);
        }
        if (jobs.empty()) {
            report_pct(0, 1, tr("No D2*.MPQ files found in the selected "
                                "sources."));
            finish();
            return;
        }

        std::vector<Job> unique;
        for (auto& job : jobs) {
            bool dup = false;
            for (const auto& unique_job : unique)
                if (unique_job.out_name == job.out_name) { dup = true; break; }
            if (!dup) unique.push_back(std::move(job));
        }

        std::uint64_t total = 0;
        for (const auto& job : unique) total += job.size;

        std::uint64_t written = 0;
        std::size_t copied = 0, skipped = 0;
        for (const auto& job : unique) {
            const fs::path out = dest / job.out_name;
            const bool already = fs::exists(out, error) &&
                                 fs::file_size(out, error) == job.size;
            report_pct(written, total,
                (already ? tr("skip %1 (already installed)")
                         : tr("copy %1  (%2 / %3 MB)"))
                    .arg(QString::fromStdString(job.out_name))
                    .arg(written / (1024 * 1024))
                    .arg(total   / (1024 * 1024)));
            if (already) ++skipped;
            else { copy_one(job, out); ++copied; }
            written += job.size;
        }
        report_pct(total, total,
            tr("Done. Copied %1, skipped %2 (already installed).")
                .arg(copied).arg(skipped));
        finish();
    }

    void finish() {
        QMetaObject::invokeMethod(this, [this] {
            summary_->setText(tr("Install directory: %1")
                .arg(field("dest").toString()));
            done_.store(true);
            emit completeChanged();
        }, Qt::QueuedConnection);
    }

    static void collect_from_iso(const std::filesystem::path& iso,
                                 std::vector<Job>& out) {
        auto opened = iso9660::Reader::open(iso);
        if (!opened) return;
        auto reader = std::make_shared<iso9660::Reader>(std::move(*opened));
        for (const auto& entry : reader->entries()) {
            if (entry.directory) continue;
            auto name = dest_target(path_basename(entry.path));
            if (name.empty()) continue;
            out.push_back({std::move(name), entry.size, reader, entry, {}});
        }
    }
    static void collect_from_dir(const std::filesystem::path& dir,
                                 std::vector<Job>& out) {
        std::error_code error;
        for (const auto& dir_entry : std::filesystem::recursive_directory_iterator(
                dir, std::filesystem::directory_options::skip_permission_denied,
                error)) {
            if (!dir_entry.is_regular_file(error)) continue;
            auto name = dest_target(dir_entry.path().filename().string());
            if (name.empty()) continue;
            out.push_back({std::move(name),
                           static_cast<std::uint64_t>(dir_entry.file_size(error)),
                           {}, {}, dir_entry.path()});
        }
    }
    static void copy_one(const Job& job, const std::filesystem::path& out) {
        std::error_code error;
        std::filesystem::create_directories(out.parent_path(), error);
        if (job.reader) {
            auto bytes = job.reader->read(job.entry);
            std::ofstream(out, std::ios::binary).write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        } else {
            std::filesystem::copy_file(
                job.local, out,
                std::filesystem::copy_options::overwrite_existing, error);
        }
    }
    void report_pct(std::uint64_t done, std::uint64_t total,
                    const QString& msg) {
        const int pct = total ? static_cast<int>((done * 100ull) / total) : 0;
        QMetaObject::invokeMethod(this, [this, pct, msg] {
            bar_->setValue(pct); status_->setText(msg);
        }, Qt::QueuedConnection);
    }

    QProgressBar* bar_{};
    QLabel* status_{};
    QLabel* summary_{};
    std::atomic<bool> done_{false};
    Q_OBJECT
};

// ---------------------------------------------------------------------------
// Wizard shell.
// ---------------------------------------------------------------------------
class InstallerWizard : public QWizard {
public:
    explicit InstallerWizard(const QString& initial_dest, QWidget* parent) :
            QWizard(parent) {
        setWindowTitle(tr("Install Diablo II"));
        addPage(new SourcePage);
        addPage(new DestPage(initial_dest));
        addPage(new CopyPage);
        resize(720, 520);
    }
    Q_OBJECT
};

// ===========================================================================
// Main window
// ===========================================================================
class MainWindow : public QWidget {
    static constexpr int kWindowWidth = 560;
public:
    MainWindow() {
        setWindowTitle(tr("D2Decomp Launcher"));

        status_ = new QLabel(this);
        status_->setWordWrap(true);
        fix_ = new QPushButton(tr("Fix: use LODPatch_114d.exe..."), this);
        fix_->setToolTip(tr("d2d reads the 1.14d tables from the patch installer; your install stays as it is."));
        connect(fix_, &QPushButton::clicked, this, &MainWindow::pickPatch);

        install_ = new QPushButton(tr("Install / Reinstall..."), this);
        connect(install_, &QPushButton::clicked, this, &MainWindow::openInstaller);

        // --- installs found on this machine --------------------------------
        found_ = new QListWidget(this);
        auto* use = new QPushButton(tr("Use selected"), this);
        connect(use, &QPushButton::clicked, this, [this] {
            const int row = found_->currentRow();
            if (row >= 0 && row < int(installs_.size()) && d2d::install::usable(installs_[std::size_t(row)]))
                choose(installs_[std::size_t(row)], QString::fromStdString("detected:" + installs_[std::size_t(row)].source));
        });
        connect(found_, &QListWidget::itemDoubleClicked, use, &QPushButton::click);
        // Search more places = Browse: the pick joins the list, checked.
        auto* more = new QPushButton(tr("Search more places..."), this);
        connect(more, &QPushButton::clicked, this, [this] {
            QFileDialog dlg(this, tr("Game data directory"), path_);
            dlg.setFileMode(QFileDialog::Directory);
            dlg.setOption(QFileDialog::ShowDirsOnly, true);
            dlg.setOptions(macos_dlg_opts() | dlg.options());
            if (dlg.exec() != QDialog::Accepted) return;
            const auto sel = dlg.selectedFiles();
            if (!sel.isEmpty()) browsed(sel.first());
        });
        auto* rescan = new QPushButton(tr("Rescan"), this);
        connect(rescan, &QPushButton::clicked, this, &MainWindow::scan);
        foundBox_ = new QWidget(this);   // always shown: Rescan lives here
        auto* foundLayout = new QVBoxLayout(foundBox_);
        foundLayout->setContentsMargins(0, 0, 0, 0);
        foundLayout->addWidget(new QLabel(tr("Diablo II installs found:"), foundBox_));
        foundLayout->addWidget(found_);
        auto* foundRow = new QHBoxLayout;
        foundRow->addWidget(use);
        foundRow->addWidget(install_);
        foundRow->addStretch();
        foundRow->addWidget(more);
        foundRow->addWidget(rescan);
        foundLayout->addLayout(foundRow);

        // --- action buttons ------------------------------------------------
        fetchBins_ = new QPushButton(tr("Fetch patch..."), this);
        fetchBins_->setVisible(
            !QString::fromUtf8(d2::kPatchUrl).isEmpty());
        connect(fetchBins_, &QPushButton::clicked, this,
                &MainWindow::fetchPatch);

        // d2d.cfg's `fullscreen`, the same key d2d's Alt+Enter writes.
        fullscreen_ = new QCheckBox(tr("Fullscreen"), this);
        fullscreen_->setToolTip(tr("Borderless fullscreen (Alt+Enter switches in game)"));
        connect(fullscreen_, &QCheckBox::toggled, this, [this](bool checked) {
            const auto cfg = d2d::userdir::user_dir("d2d") / "d2d.cfg";
            if (!d2d::userdir::save_cfg(cfg, { { "fullscreen", checked ? "1" : "0" } }))
                QMessageBox::warning(this, tr("Settings"), tr("Could not write %1").arg(from_path(cfg)));
        });

        launch_ = new QPushButton(tr("Launch"), this);
        launch_->setDefault(true);
        connect(launch_, &QPushButton::clicked, this, &MainWindow::launch);

        // --- version + update line -----------------------------------------
        version_ = new QLabel(this);
        version_->setStyleSheet("color: gray;");
        version_->setText(QString::fromUtf8(d2::kAppVersion));   // v0.1.0-g42200f5[-dirty]
        upgrade_ = new QPushButton(tr("Update available"), this);
        upgrade_->setHidden(true);
        connect(upgrade_, &QPushButton::clicked, this, [this] {
            if (!latest_url_.isEmpty())
                QDesktopServices::openUrl(QUrl(latest_url_));
        });

        // --- layout --------------------------------------------------------
        // Banner flush with the top and sides: the root layout has no
        // margins; `layout` puts them back for the controls. The width is
        // fixed so the banner always spans it (baked at 2x for retina).
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        auto* hero = new QLabel(this);
        QPixmap banner(":/hero.png");
        banner = banner.scaledToWidth(2 * kWindowWidth, Qt::SmoothTransformation);
        banner.setDevicePixelRatio(2.0);
        hero->setPixmap(banner);
        root->addWidget(hero);
        auto* layout = new QVBoxLayout;
        layout->setContentsMargins(11, 8, 11, 11);
        root->addLayout(layout);

        layout->addWidget(foundBox_);
        auto* statusRow = new QHBoxLayout;
        statusRow->addWidget(status_, 1);
        statusRow->addWidget(fix_);
        layout->addLayout(statusRow);

        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        layout->addSpacing(6);
        layout->addWidget(line);

        auto* btnRow = new QHBoxLayout;
        btnRow->addWidget(fetchBins_);
        btnRow->addStretch();
        btnRow->addWidget(fullscreen_);
        btnRow->addSpacing(12);   // macOS clips the checkbox label against the default button
        btnRow->addWidget(launch_);
        layout->addLayout(btnRow);
        layout->addStretch();

        auto* footer = new QHBoxLayout;
        footer->addWidget(version_);
        footer->addStretch();
        footer->addWidget(upgrade_);
        layout->addLayout(footer);

        setFixedWidth(kWindowWidth);
        resize(kWindowWidth, 460);

        loadSettings();
        refresh();
        // A saved install that still classifies as usable is kept; the
        // list still shows what else is on the machine.
        const auto saved = classify_dir(path_);
        if (saved && d2d::install::usable(*saved)) persist();   // d2d.cfg follows the launcher
        scan();
        startUpdateCheck();
    }

    // A scan still running posts to us: wait so it never outlives `this`
    // (Qt drops the queued call once we're gone).
    ~MainWindow() override { QThreadPool::globalInstance()->waitForDone(); saveSettings(); }

private:
    void loadSettings() {
        QSettings settings;
        path_ = settings.value("game/dataPath").toString();
        patch_path_ = settings.value("game/patchPath").toString();
        d2d::userdir::Config cfg;
        d2d::userdir::load_cfg(d2d::userdir::user_dir("d2d") / "d2d.cfg", cfg);
        if (patch_path_.isEmpty())   // a hand-set `patch =` in d2d.cfg counts
            patch_path_ = QString::fromStdString(cfg["patch"]);
        const QSignalBlocker quiet(fullscreen_);   // loading isn't a change to write back
        fullscreen_->setChecked(cfg["fullscreen"] == "1");
        source_ = settings.value("game/source", "manual").toString();
    }
    void saveSettings() const {
        QSettings settings;
        settings.setValue("game/dataPath", path_);
        settings.setValue("game/patchPath", patch_path_);
        settings.setValue("game/source", source_);
        settings.sync();   // on disk now, not at some later event-loop tick
    }
    // QSettings always; d2d.cfg's `data` / `patch` only for an install d2d
    // can use (d2d stops on a bad `data =`).
    // ponytail: no patch chosen leaves d2d.cfg's `patch` as it is (a
    // hand-set LODPatch_114d.exe survives); a stale one is the user's to clear.
    bool persist() {
        saveSettings();
        const auto install = classify_dir(path_);
        if (!install || !d2d::install::usable(*install)) return true;
        const auto cfg = d2d::userdir::user_dir("d2d") / "d2d.cfg";
        std::vector<std::pair<std::string, std::string>> values{ { "data", install->dir.string() } };
        if (!patch_path_.isEmpty()) values.emplace_back("patch", to_path(patch_path_).string());
        if (d2d::userdir::save_cfg(cfg, values)) return true;
        QMessageBox::warning(this, tr("Settings"), tr("Could not write %1").arg(from_path(cfg)));
        return false;
    }

    // Look for installs off the UI thread; results land in onDetected.
    // ponytail: no time budget, a slow network drive in a probed folder
    // stalls the list (not the window); add a 2 s cutoff if that bites.
    void scan() {
        status_->setText(tr("Looking for Diablo II..."));
        status_->setStyleSheet("color: gray;");
        QThreadPool::globalInstance()->start([this] {
            auto detected = d2d::install::detect(d2d::install::system_environment(false));
            QMetaObject::invokeMethod(this, [this, found = std::move(detected)]() mutable { onDetected(std::move(found)); },
                                      Qt::QueuedConnection);
        });
    }

    // No usable install chosen yet and exactly one 1.14d found: take it.
    // The list always shows them all, the unusable ones greyed out with why.
    void onDetected(std::vector<d2d::install::Install> found) {
        installs_ = std::move(found);
        const auto is_114d = [](const auto& install) {
            return d2d::install::usable(install) && install.version == d2d::install::Version::v114d;
        };
        const auto current = classify_dir(path_);
        if (!(current && d2d::install::usable(*current))
            && std::count_if(installs_.begin(), installs_.end(), is_114d) == 1) {
            const auto& install = *std::find_if(installs_.begin(), installs_.end(), is_114d);
            choose(install, QString::fromStdString("detected:" + install.source));
        }
        fillList();
        refresh();
    }

    // Every install found, plus the chosen one (browsed / installed) if the
    // scan missed it; the chosen one wears the green check.
    void fillList() {
        const auto current = classify_dir(path_);
        // Same folder, any spelling (trailing slash, symlink).
        const auto is_current = [&](const auto& install) {
            std::error_code error;
            return current && std::filesystem::equivalent(install.dir, current->dir, error);
        };
        if (current && std::none_of(installs_.begin(), installs_.end(), is_current)) installs_.push_back(*current);
        found_->clear();
        for (const auto& install : installs_) {
            auto* item = new QListWidgetItem(QString::fromStdString(d2d::install::title(install)) + "  —  "
                                             + from_path(install.dir), found_);
            const auto problem = QString::fromStdString(d2d::install::problem(install));
            const auto source = QString::fromStdString(install.source);
            item->setToolTip(problem.isEmpty() ? source : problem + "\n" + source);
            if (is_current(install)) {
                item->setIcon(check_icon());
                found_->setCurrentItem(item);
            }
            if (!d2d::install::usable(install)) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            else if (!problem.isEmpty() && !is_current(install))
                item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
        }
        if (installs_.empty()) {
            auto* item = new QListWidgetItem(tr("No Diablo II install found. Search more places, or install from the discs."), found_);
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
    }

    static QIcon check_icon() {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        auto font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor(0, 160, 0));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, QString::fromUtf8("✓"));
        return QIcon(pixmap);
    }

    void choose(const d2d::install::Install& install, const QString& source) {
        source_ = source;
        // The UAC VirtualStore copy of patch_d2.mpq lives outside the dir:
        // d2d only finds it as an explicit patch layer.
        const bool outside = !install.patch_mpq.empty() && install.patch_mpq.parent_path() != install.dir;
        patch_path_ = outside ? from_path(install.patch_mpq) : QString{};
        path_ = from_path(install.dir);
        persist();
        fillList();
        refresh();
    }

    // Browse: a folder, Game.exe or Diablo II.app, classified like a find.
    void browsed(const QString& picked) {
        const QString dir = normalize_pick(picked);
        const auto install = classify_dir(dir);
        if (install && !d2d::install::usable(*install)) {
            QMessageBox::warning(this, tr("Can't use this install"),
                QString::fromStdString(d2d::install::title(*install) + ": " + d2d::install::problem(*install)));
            return;
        }
        if (install) { choose(*install, "manual"); return; }
        source_ = "manual";
        patch_path_.clear();
        path_ = dir;           // not an install (yet): Install can fill it
        saveSettings();
        fillList();
        refresh();
    }

    // Not 1.14d: point the patch layer at a 1.14d patch installer. The
    // install is never touched; d2d reads the patched tables from the .exe.
    void pickPatch() {
        QFileDialog dlg(this, tr("LODPatch_114d.exe"), QDir::homePath(), tr("Patch installer (*.exe *.mpq)"));
        dlg.setFileMode(QFileDialog::ExistingFile);
        dlg.setOptions(macos_dlg_opts() | dlg.options());
        if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return;
        const QString picked = dlg.selectedFiles().first();
        if (!picked.endsWith(".mpq", Qt::CaseInsensitive)) {
            try {
                (void)d2d::mpq::Archive::installer(to_path(picked));
            } catch (const std::exception&) {
                QMessageBox::warning(this, tr("Cannot open"),
                    tr("Not a Blizzard MPQ-appended installer (or corrupt): %1").arg(picked));
                return;
            }
        }
        patch_path_ = picked;
        persist();
        refresh();
    }

    void refresh() {
        const auto install = classify_dir(path_);
        const bool ok = install && d2d::install::usable(*install);
        QString msg;
        if (!install) {
            msg = path_.isEmpty() ? tr("⚠ No game data chosen. Pick an install, Search more places, or click Install.")
                                          : tr("⚠ No game data at this path. Click Install to set it up.");
        } else {
            msg = (ok ? tr("✓ ") : tr("⚠ ")) + QString::fromStdString(d2d::install::title(*install));
            if (source_.startsWith("detected:")) msg += tr("  (found: %1)").arg(source_.mid(9));
            const auto problem = QString::fromStdString(d2d::install::problem(*install));
            if (!problem.isEmpty()) msg += "\n" + problem;
        }
        const bool patched = !patch_path_.isEmpty() && QFileInfo::exists(patch_path_);
        if (ok && !patch_path_.isEmpty())
            msg += "\n" + (patched ? tr("Patch layer: %1").arg(patch_path_) : tr("⚠ Patch layer missing: %1").arg(patch_path_));
        const bool needs_patch = ok && !d2d::install::problem(*install).empty();   // not 1.14d, or no patch_d2.mpq
        status_->setText(msg);
        status_->setStyleSheet(ok && (!needs_patch || patched) ? "color: green;" : "color: orange;");
        fix_->setVisible(needs_patch && !patched);
        // Launch needs the d2d binary and game data d2d can use.
        const QString engine = engineBinaryPath();
        launch_->setEnabled(ok && !engine.isEmpty());
        launch_->setToolTip(
            !ok              ? tr("Install the game first.")
          : engine.isEmpty() ? tr("d2d not found beside the launcher.")
          : tr("Launch %1").arg(engine));
    }

    void openInstaller() {
        InstallerWizard wiz(path_, this);
        if (wiz.exec() == QDialog::Accepted) {
            source_ = "installed";
            patch_path_.clear();
            path_ = wiz.field("dest").toString();
            persist();
            fillList();
            refresh();
        }
    }

    // d2d reads `data` / `patch` from d2d.cfg, which persist() keeps current.
    // Config is flushed first, then the launcher gets out of the way; d2d
    // raises its own window (window.hpp) so it lands in front.
    void launch() {
        if (!persist()) return;
        const QString engine = engineBinaryPath();
        if (!QProcess::startDetached(engine, {}, QFileInfo(engine).absolutePath())) {
            QMessageBox::warning(this, tr("Launch"), tr("Could not start %1").arg(engine));
            return;
        }
        close();
    }

    void fetchPatch() {
        const QString url = QString::fromUtf8(d2::kPatchUrl);
        if (url.isEmpty()) return;

        const QString suggested = QDir::homePath() + "/" +
            QFileInfo(QUrl(url).path()).fileName();
        QString dst = QFileDialog::getSaveFileName(
            this, tr("Save downloaded installer"),
            suggested.isEmpty() ? QDir::homePath() + "/patch.exe" : suggested);
        if (dst.isEmpty()) return;
        auto* file = new QFile(dst, this);
        if (!file->open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, tr("Cannot write"),
                tr("Cannot open %1 for writing.").arg(dst));
            file->deleteLater();
            return;
        }

        auto* mgr = new QNetworkAccessManager(this);
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("User-Agent", "d2d-launcher");
        auto* reply = mgr->get(req);

        auto* prog = new QProgressDialog(
            tr("Downloading patch installer..."), tr("Cancel"), 0, 0, this);
        prog->setWindowModality(Qt::WindowModal);
        prog->show();

        connect(reply, &QNetworkReply::downloadProgress, prog,
                [prog](qint64 got, qint64 total) {
            if (total > 0) { prog->setMaximum(int(total)); prog->setValue(int(got)); }
        });
        connect(reply, &QIODevice::readyRead, file,
                [reply, file] { file->write(reply->readAll()); });
        connect(prog, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, file, prog, dst] {
            file->write(reply->readAll());
            file->close();
            prog->close();
            prog->deleteLater();
            file->deleteLater();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!ok) {
                QMessageBox::warning(this, tr("Download failed"),
                    tr("Could not download the installer. Try a manual download."));
                return;
            }
            QMessageBox::information(this, tr("Download complete"),
                tr("Saved to %1.\n\nNow click \"Fix: use LODPatch_114d.exe...\" "
                   "and pick this file.").arg(dst));
        });
    }

    // d2d beside the launcher, beside the macOS .app bundle, or in the
    // build tree (apps/launcher → apps/d2d, incl. MSVC's Release/ dirs).
    static QString engineBinaryPath() {
        const QDir dir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
        const QString name = "d2d.exe";
#else
        const QString name = "d2d";
#endif
        for (const auto* relative : {".", "../../..", "../../../../d2d", "../d2d", "../../d2d/Release", "../../d2d/Debug"}) {
            const QFileInfo info(dir.filePath(QString(relative) + "/" + name));
            if (info.isFile() && info.isExecutable()) return info.canonicalFilePath();
        }
        return {};
    }

    void startUpdateCheck() {
        const QString url = QString::fromUtf8(d2::kUpdateApiUrl);
        if (url.isEmpty()) return;
        auto* mgr = new QNetworkAccessManager(this);
        connect(mgr, &QNetworkAccessManager::finished,
                this, &MainWindow::onUpdateReply);
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("Accept", "application/vnd.github+json");
        req.setRawHeader("User-Agent", "d2d-launcher");
        mgr->get(req);
    }

    void onUpdateReply(QNetworkReply* reply) {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = object.value("tag_name").toString();
        latest_url_ = object.value("html_url").toString();
        if (tag.isEmpty()) return;
        const QString local = QString::fromUtf8(d2::kAppVersion);
        if (tag == local || local.startsWith(tag)) return;
        upgrade_->setText(tr("Update available: %1").arg(tag));
        upgrade_->setHidden(false);
    }

    QString path_;   // the chosen game data dir
    QLabel* status_{};
    QPushButton* fix_{};
    QListWidget* found_{};
    QWidget* foundBox_{};
    std::vector<d2d::install::Install> installs_;
    QString patch_path_, source_ = "manual";
    QLabel* version_{};
    QPushButton* install_{};
    QPushButton* fetchBins_{};
    QPushButton* launch_{};
    QCheckBox* fullscreen_{};
    QPushButton* upgrade_{};
    QString latest_url_;
    Q_OBJECT
};

// ===========================================================================
// Entry point.
// ===========================================================================
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(":/d2d.png"));
    QApplication::setOrganizationName("D2Decomp");
    QApplication::setApplicationName("D2 Launcher");

    MainWindow window;
    window.show();
    return app.exec();
}

#include "main.moc"
