// D2Decomp launcher — see docs/PLAN.md.

#include "config.hpp"
#include <iso9660.hpp>
#include <StormLib.h>

#include <QAbstractButton>
#include <QApplication>
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
#include <QProgressDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

#include <atomic>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static std::string mpq_target(std::string_view basename) {
    std::string upper(basename);
    for (auto& c : upper) c = static_cast<char>(std::toupper(c));
    if (upper.size() > 4 && upper.starts_with("D2") &&
        upper.ends_with(".MPQ")) {
        std::string lower(basename);
        for (auto& c : lower) c = static_cast<char>(std::tolower(c));
        return lower;
    }
    return {};
}

static std::string runtime_target(std::string_view basename) {
    std::string upper(basename);
    for (auto& c : upper) c = static_cast<char>(std::toupper(c));
    static constexpr std::string_view kBins[] = {
        "GAME.EXE", "DIABLO II.EXE",
        "D2CLIENT.DLL", "D2COMMON.DLL", "D2GAME.DLL", "D2GFX.DLL",
        "D2LANG.DLL",   "D2LAUNCH.DLL", "D2MCPCLIENT.DLL",
        "D2MULTI.DLL",  "D2NET.DLL",    "D2SERVER.DLL",
        "D2SOUND.DLL",  "D2WIN.DLL",
        "BNCLIENT.DLL", "FOG.DLL",      "STORM.DLL",  "IJL11.DLL",
        "BINKW32.DLL",  "SMACKW32.DLL",
    };
    for (auto b : kBins) if (upper == b) {
        std::string lower(basename);
        for (auto& c : lower) c = static_cast<char>(std::tolower(c));
        return "bin/" + lower;
    }
    return {};
}

static std::string dest_target(std::string_view basename) {
    if (auto n = mpq_target(basename);     !n.empty()) return n;
    if (auto n = runtime_target(basename); !n.empty()) return n;
    return {};
}

static std::string path_basename(const std::string& p) {
    const auto slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

static bool is_iso_path(const QString& p) {
    return QFileInfo(p).suffix().compare("iso", Qt::CaseInsensitive) == 0;
}

static QString default_dest() {
    auto base = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) base = QDir::homePath();
    return base + "/game";
}

// Scan a PE for its VS_VERSION_INFO "FileVersion" string. UTF-16LE search
// avoids a full PE resource walk. Empty return = not found / not a PE.
static QString pe_file_version(const std::filesystem::path& file) {
    QFile f(QString::fromStdString(file.string()));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const auto data = f.readAll();
    static constexpr char16_t needle[] = u"FileVersion";
    constexpr int needle_bytes = sizeof(needle) - sizeof(char16_t);
    for (int i = 0; i + needle_bytes < data.size(); i += 2) {
        if (std::memcmp(data.constData() + i, needle, needle_bytes) != 0)
            continue;
        int p = i + needle_bytes + 2;         // skip "FileVersion\0"
        while (p < data.size() && (p % 4) != 0) ++p;
        QString v;
        while (p + 1 < data.size() && v.size() < 64) {
            const auto c = static_cast<char16_t>(
                quint8(data[p]) | (quint8(data[p + 1]) << 8));
            if (c == 0) break;
            v.append(QChar(c));
            p += 2;
        }
        if (!v.isEmpty()) {
            return v.trimmed().replace(", ", ".").replace(",", ".");
        }
    }
    return {};
}

static bool game_dir_valid(const QString& dir) {
    if (dir.isEmpty()) return false;
    QDir d(dir);
    if (!d.exists()) return false;
    const auto hits = d.entryList({"d2data.mpq", "D2DATA.MPQ"},
                                   QDir::Files | QDir::CaseSensitive);
    return !hits.isEmpty();
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
            for (const auto& f : dlg.selectedFiles()) {
                add_unique(f);
                remember_dir(QFileInfo(f).absolutePath());
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
            const QString d = sel.first();
            remember_dir(d);
            const QDir dir(d);
            const auto isos = dir.entryList({"*.iso", "*.ISO"},
                                            QDir::Files, QDir::Name);
            if (!isos.isEmpty()) {
                for (const auto& f : isos) add_unique(dir.absoluteFilePath(f));
            } else {
                add_unique(d);
            }
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            qDeleteAll(list_->selectedItems());
            emit completeChanged();
        });

        auto* v = new QVBoxLayout(this);
        v->addWidget(list_);
        auto* row = new QHBoxLayout;
        row->addWidget(add_isos);
        row->addWidget(add_dir);
        row->addStretch();
        row->addWidget(remove);
        v->addLayout(row);

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
    void remember_dir(const QString& d) { last_dir_ = d; }
    void add_unique(const QString& p) {
        if (list_->findItems(p, Qt::MatchExactly).isEmpty()) list_->addItem(p);
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

        auto* v = new QVBoxLayout(this);
        auto* row = new QHBoxLayout;
        row->addWidget(edit_);
        row->addWidget(browse);
        v->addLayout(row);
        v->addStretch();

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
        auto* v = new QVBoxLayout(this);
        v->addWidget(bar_);
        v->addWidget(status_);
        v->addWidget(summary_);
        v->addStretch();
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
        std::error_code ec;
        fs::create_directories(dest, ec);

        std::vector<Job> jobs;
        for (const auto& s : sources) {
            const fs::path p(s.toStdString());
            if (is_iso_path(s))                collect_from_iso(p, jobs);
            else if (fs::is_directory(p, ec))  collect_from_dir(p, jobs);
            else                               collect_from_iso(p, jobs);
        }
        if (jobs.empty()) {
            report_pct(0, 1, tr("No D2*.MPQ files found in the selected "
                                "sources."));
            finish();
            return;
        }

        std::vector<Job> unique;
        for (auto& j : jobs) {
            bool dup = false;
            for (const auto& u : unique)
                if (u.out_name == j.out_name) { dup = true; break; }
            if (!dup) unique.push_back(std::move(j));
        }

        std::uint64_t total = 0;
        for (const auto& j : unique) total += j.size;

        std::uint64_t written = 0;
        std::size_t copied = 0, skipped = 0;
        for (const auto& j : unique) {
            const fs::path out = dest / j.out_name;
            const bool already = fs::exists(out, ec) &&
                                 fs::file_size(out, ec) == j.size;
            report_pct(written, total,
                (already ? tr("skip %1 (already installed)")
                         : tr("copy %1  (%2 / %3 MB)"))
                    .arg(QString::fromStdString(j.out_name))
                    .arg(written / (1024 * 1024))
                    .arg(total   / (1024 * 1024)));
            if (already) ++skipped;
            else { copy_one(j, out); ++copied; }
            written += j.size;
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
        auto r = iso9660::Reader::open(iso);
        if (!r) return;
        auto reader = std::make_shared<iso9660::Reader>(std::move(*r));
        for (const auto& e : reader->entries()) {
            if (e.directory) continue;
            auto name = dest_target(path_basename(e.path));
            if (name.empty()) continue;
            out.push_back({std::move(name), e.size, reader, e, {}});
        }
    }
    static void collect_from_dir(const std::filesystem::path& dir,
                                 std::vector<Job>& out) {
        std::error_code ec;
        for (const auto& de : std::filesystem::recursive_directory_iterator(
                dir, std::filesystem::directory_options::skip_permission_denied,
                ec)) {
            if (!de.is_regular_file(ec)) continue;
            auto name = dest_target(de.path().filename().string());
            if (name.empty()) continue;
            out.push_back({std::move(name),
                           static_cast<std::uint64_t>(de.file_size(ec)),
                           {}, {}, de.path()});
        }
    }
    static void copy_one(const Job& j, const std::filesystem::path& out) {
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        if (j.reader) {
            auto bytes = j.reader->read(j.entry);
            std::ofstream(out, std::ios::binary).write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        } else {
            std::filesystem::copy_file(
                j.local, out,
                std::filesystem::copy_options::overwrite_existing, ec);
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
public:
    MainWindow() {
        setWindowTitle(tr("D2Decomp Launcher"));
        resize(560, 340);

        // --- game data path row --------------------------------------------
        auto* pathLabel = new QLabel(tr("Game data:"), this);
        path_ = new QLineEdit(this);
        auto* browse = new QPushButton(tr("Browse..."), this);
        connect(browse, &QPushButton::clicked, this, [this] {
            QFileDialog dlg(this, tr("Game data directory"), path_->text());
            dlg.setFileMode(QFileDialog::Directory);
            dlg.setOption(QFileDialog::ShowDirsOnly, true);
            dlg.setOptions(macos_dlg_opts() | dlg.options());
            if (dlg.exec() != QDialog::Accepted) return;
            const auto sel = dlg.selectedFiles();
            if (!sel.isEmpty()) path_->setText(sel.first());
        });
        connect(path_, &QLineEdit::textChanged, this, &MainWindow::refresh);

        status_ = new QLabel(this);
        status_->setWordWrap(true);

        // --- action buttons ------------------------------------------------
        install_ = new QPushButton(tr("Install / Reinstall..."), this);
        connect(install_, &QPushButton::clicked, this, &MainWindow::openInstaller);

        addBins_ = new QPushButton(tr("Add patch binaries..."), this);
        connect(addBins_, &QPushButton::clicked, this,
                &MainWindow::addPatchBinaries);

        fetchBins_ = new QPushButton(tr("Fetch patch..."), this);
        fetchBins_->setVisible(
            !QString::fromUtf8(d2::kPatchUrl).isEmpty());
        connect(fetchBins_, &QPushButton::clicked, this,
                &MainWindow::fetchPatch);

        launch_ = new QPushButton(tr("Launch"), this);
        launch_->setDefault(true);
        connect(launch_, &QPushButton::clicked, this, &MainWindow::launch);

        // --- version + update line -----------------------------------------
        version_ = new QLabel(this);
        version_->setStyleSheet("color: gray;");
        version_->setText(tr("v%1  (%2)")
            .arg(QString::fromUtf8(d2::kAppVersion))
            .arg(QString::fromUtf8(d2::kGitCommit)));
        upgrade_ = new QPushButton(tr("Update available"), this);
        upgrade_->setHidden(true);
        connect(upgrade_, &QPushButton::clicked, this, [this] {
            if (!latest_url_.isEmpty())
                QDesktopServices::openUrl(QUrl(latest_url_));
        });

        // --- layout --------------------------------------------------------
        auto* v = new QVBoxLayout(this);

        auto* pathRow = new QHBoxLayout;
        pathRow->addWidget(pathLabel);
        pathRow->addWidget(path_, 1);
        pathRow->addWidget(browse);
        v->addLayout(pathRow);
        v->addWidget(status_);

        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        v->addSpacing(6);
        v->addWidget(line);

        auto* btnRow = new QHBoxLayout;
        btnRow->addWidget(install_);
        btnRow->addWidget(addBins_);
        btnRow->addWidget(fetchBins_);
        btnRow->addStretch();
        btnRow->addWidget(launch_);
        v->addLayout(btnRow);
        v->addStretch();

        auto* footer = new QHBoxLayout;
        footer->addWidget(version_);
        footer->addStretch();
        footer->addWidget(upgrade_);
        v->addLayout(footer);

        loadSettings();
        refresh();
        startUpdateCheck();
    }

    ~MainWindow() override { saveSettings(); }

private:
    void loadSettings() {
        QSettings s;
        path_->setText(s.value("game/dataPath", default_dest()).toString());
    }
    void saveSettings() const {
        QSettings s;
        s.setValue("game/dataPath", path_->text());
    }

    void refresh() {
        const bool ok = game_dir_valid(path_->text());
        QString msg = ok
            ? tr("✓ Game data found — d2data.mpq detected.")
            : tr("⚠ No game data at this path. Click Install to set it up.");
        if (ok) {
            namespace fs = std::filesystem;
            const auto bin = fs::path(path_->text().toStdString()) / "bin";
            QString ver;
            for (const auto& probe : {"game.exe", "d2client.dll", "d2common.dll"}) {
                ver = pe_file_version(bin / probe);
                if (!ver.isEmpty()) break;
            }
            msg += ver.isEmpty()
                ? tr("  (no patch binaries yet)")
                : tr("  Diablo II v%1").arg(ver);
        }
        status_->setText(msg);
        status_->setStyleSheet(ok ? "color: green;" : "color: orange;");
        // Launch is grayed until the engine binary exists AND game data is
        // present. Engine binary lives beside the launcher; there is none
        // yet, so Launch stays disabled and says why.
        const QString engine = engineBinaryPath();
        const bool haveEngine = !engine.isEmpty() &&
                                QFileInfo(engine).isExecutable();
        launch_->setEnabled(ok && haveEngine);
        launch_->setToolTip(
            !ok       ? tr("Install the game first.")
          : !haveEngine ? tr("Engine binary not built yet (phase 5).")
          : tr("Launch the game."));
    }

    void openInstaller() {
        InstallerWizard wiz(path_->text(), this);
        if (wiz.exec() == QDialog::Accepted) {
            path_->setText(wiz.field("dest").toString());
            saveSettings();
            refresh();
        }
    }

    void launch() {
        QMessageBox::information(this, tr("Launch"),
            tr("Engine not built yet — coming in phase 5."));
    }

    void addPatchBinaries() {
        QFileDialog dlg(this, tr("Point at patch installer or install folder"),
                        QDir::homePath());
        dlg.setFileMode(QFileDialog::AnyFile);
        dlg.setOptions(macos_dlg_opts() | dlg.options());
        if (dlg.exec() != QDialog::Accepted) return;
        const auto sel = dlg.selectedFiles();
        if (sel.isEmpty()) return;
        const QString p = sel.first();
        const QFileInfo fi(p);
        int n = 0;
        if (fi.isDir()) {
            n = importBinariesFromDir(p, path_->text());
        } else if (fi.isFile()) {
            n = importBinariesFromExe(p, path_->text());
            if (n < 0) {
                QMessageBox::warning(this, tr("Cannot open"),
                    tr("Not a Blizzard MPQ-appended installer (or corrupt): %1")
                        .arg(p));
                return;
            }
        } else {
            return;
        }
        namespace fs = std::filesystem;
        const auto bin = fs::path(path_->text().toStdString()) / "bin";
        QString ver;
        for (const auto& probe : {"game.exe", "d2client.dll", "d2common.dll"}) {
            ver = pe_file_version(bin / probe);
            if (!ver.isEmpty()) break;
        }
        QMessageBox::information(this, tr("Import complete"),
            ver.isEmpty()
                ? tr("✓ Imported %1 D2 binaries into %2/bin/")
                    .arg(n).arg(path_->text())
                : tr("✓ Imported %1 D2 binaries (v%2) into %3/bin/")
                    .arg(n).arg(ver).arg(path_->text()));
        refresh();
    }

    static int importBinariesFromDir(const QString& src, const QString& dst) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(fs::path(dst.toStdString()) / "bin", ec);
        int n = 0;
        for (const auto& de : fs::recursive_directory_iterator(
                src.toStdString(),
                fs::directory_options::skip_permission_denied, ec)) {
            if (!de.is_regular_file(ec)) continue;
            auto name = runtime_target(de.path().filename().string());
            if (name.empty()) continue;
            const fs::path out = fs::path(dst.toStdString()) / name;
            fs::copy_file(de.path(), out,
                          fs::copy_options::overwrite_existing, ec);
            if (!ec) ++n;
        }
        return n;
    }

    // Returns -1 if the file cannot be opened as an MPQ, else count extracted.
    static int importBinariesFromExe(const QString& exe, const QString& dst) {
        namespace fs = std::filesystem;
        HANDLE mpq{};
        const auto exeUtf8 = exe.toStdString();
        if (!SFileOpenArchive(exeUtf8.c_str(), 0,
                              MPQ_OPEN_READ_ONLY | STREAM_FLAG_READ_ONLY, &mpq)) {
            return -1;
        }
        std::error_code ec;
        fs::create_directories(fs::path(dst.toStdString()) / "bin", ec);
        static constexpr std::string_view kBins[] = {
            "Game.exe", "Diablo II.exe",
            "D2Client.dll", "D2Common.dll", "D2Game.dll", "D2Gfx.dll",
            "D2Lang.dll",   "D2Launch.dll", "D2MCPClient.dll",
            "D2Multi.dll",  "D2Net.dll",    "D2Server.dll",
            "D2Sound.dll",  "D2Win.dll",
            "Bnclient.dll", "Fog.dll",      "Storm.dll",  "ijl11.dll",
            "BinkW32.dll",  "SmackW32.dll",
        };
        int n = 0;
        for (auto name : kBins) {
            HANDLE f{};
            const std::string namez(name);
            if (!SFileOpenFileEx(mpq, namez.c_str(), 0, &f)) continue;
            const DWORD size = SFileGetFileSize(f, nullptr);
            std::vector<char> buf(size);
            DWORD got = 0;
            SFileReadFile(f, buf.data(), size, &got, nullptr);

            SFileCloseFile(f);

            // kBins are all PEs, so they must start with "MZ". Blizzard's patch
            // installer prepends a proprietary wrapper (~24 bytes: header size,
            // flags, payload size, FILETIME) to each stored PE that its own
            // installer strips at extraction time. StormLib delivers the wrapper
            // verbatim — scan the first 64 bytes for MZ and skip anything before.
            std::size_t off = 0;
            for (std::size_t i = 0; i + 1 < std::min<std::size_t>(64, got); ++i) {
                if (buf[i] == 'M' && buf[i + 1] == 'Z') { off = i; break; }
            }

            auto target = runtime_target(name);
            if (target.empty()) continue;
            const fs::path out = fs::path(dst.toStdString()) / target;
            std::ofstream(out, std::ios::binary)
                .write(buf.data() + off, got - off);
            ++n;
        }
        SFileCloseArchive(mpq);
        return n;
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
        auto* f = new QFile(dst, this);
        if (!f->open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, tr("Cannot write"),
                tr("Cannot open %1 for writing.").arg(dst));
            f->deleteLater();
            return;
        }

        auto* mgr = new QNetworkAccessManager(this);
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("User-Agent", "d2-launcher");
        auto* reply = mgr->get(req);

        auto* prog = new QProgressDialog(
            tr("Downloading patch installer..."), tr("Cancel"), 0, 0, this);
        prog->setWindowModality(Qt::WindowModal);
        prog->show();

        connect(reply, &QNetworkReply::downloadProgress, prog,
                [prog](qint64 got, qint64 total) {
            if (total > 0) { prog->setMaximum(int(total)); prog->setValue(int(got)); }
        });
        connect(reply, &QIODevice::readyRead, f,
                [reply, f] { f->write(reply->readAll()); });
        connect(prog, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, f, prog, dst] {
            f->write(reply->readAll());
            f->close();
            prog->close();
            prog->deleteLater();
            f->deleteLater();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!ok) {
                QMessageBox::warning(this, tr("Download failed"),
                    tr("Could not download the installer. Try a manual download."));
                return;
            }
            QMessageBox::information(this, tr("Download complete"),
                tr("Saved to %1.\n\nNow click \"Add patch binaries...\" and "
                   "point at this file to extract the binaries.").arg(dst));
        });
    }

    QString engineBinaryPath() const {
        const auto dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
        const QString name = "d2.exe";
#else
        const QString name = "d2";
#endif
        const QString p = dir + "/" + name;
        return QFileInfo::exists(p) ? p : QString{};
    }

    void startUpdateCheck() {
        const QString url = QString::fromUtf8(d2::kUpdateApiUrl);
        if (url.isEmpty()) return;
        auto* mgr = new QNetworkAccessManager(this);
        connect(mgr, &QNetworkAccessManager::finished,
                this, &MainWindow::onUpdateReply);
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("Accept", "application/vnd.github+json");
        req.setRawHeader("User-Agent", "d2-launcher");
        mgr->get(req);
    }

    void onUpdateReply(QNetworkReply* r) {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return;
        const auto o = QJsonDocument::fromJson(r->readAll()).object();
        const QString tag = o.value("tag_name").toString();
        latest_url_ = o.value("html_url").toString();
        if (tag.isEmpty()) return;
        const QString local = QString::fromUtf8(d2::kAppVersion);
        if (tag == local || local.startsWith(tag)) return;
        upgrade_->setText(tr("Update available: %1").arg(tag));
        upgrade_->setHidden(false);
    }

    QLineEdit* path_{};
    QLabel* status_{};
    QLabel* version_{};
    QPushButton* install_{};
    QPushButton* addBins_{};
    QPushButton* fetchBins_{};
    QPushButton* launch_{};
    QPushButton* upgrade_{};
    QString latest_url_;
    Q_OBJECT
};

// ===========================================================================
// Entry point.
// ===========================================================================
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("D2Decomp");
    QApplication::setApplicationName("D2 Launcher");

    MainWindow w;
    w.show();
    return app.exec();
}

#include "main.moc"
