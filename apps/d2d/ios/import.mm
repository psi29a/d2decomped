// SPDX-License-Identifier: GPL-3.0-or-later
// Folder picker. Copies into the sandbox, then drops the security-scoped URL.
#import <UIKit/UIKit.h>

#include "ios/import.hpp"

#include <log.hpp>
#include <userdir.hpp>

#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

@interface D2DFolderPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, strong) NSURL* url;
@property(nonatomic, assign) BOOL finished;
@end

@implementation D2DFolderPicker
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    (void)controller;
    if (urls.count > 0) self.url = urls.firstObject;
    self.finished = YES;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    (void)controller;
    self.finished = YES;
}
@end

namespace {

bool same_name(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (std::tolower(static_cast<unsigned char>(left[index])) != std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    return true;
}

std::optional<fs::path> find_named(const fs::path& dir, std::string_view name) {
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(dir, error))
        if (!error && same_name(entry.path().filename().string(), name)) return entry.path();
    return std::nullopt;
}

NSURL* pick_folder() {
    D2DFolderPicker* delegate = [D2DFolderPicker new];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[ @"public.folder" ] inMode:UIDocumentPickerModeOpen];
#pragma clang diagnostic pop
    picker.delegate = delegate;
    picker.allowsMultipleSelection = NO;
    picker.modalPresentationStyle = UIModalPresentationFullScreen;

    UIWindow* window = nil;
    if (@available(iOS 13.0, *)) {
        for (UIScene* candidate in UIApplication.sharedApplication.connectedScenes) {
            if (![candidate isKindOfClass:[UIWindowScene class]]) continue;
            window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)candidate];
            break;
        }
    }
    if (!window) window = [[UIWindow alloc] initWithFrame:CGRectMake(0, 0, 800, 600)];
    UIViewController* root = [UIViewController new];
    window.rootViewController = root;
    [window makeKeyAndVisible];
    [root presentViewController:picker animated:NO completion:nil];

    NSRunLoop* loop = [NSRunLoop currentRunLoop];
    while (!delegate.finished)
        [loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    NSURL* url = delegate.url;
    [window setHidden:YES];
    return url;
}

bool write_data_cfg(const fs::path& dest, const fs::path& cfg_file) {
    const auto data = find_named(dest, "d2data.mpq");
    if (!data) return false;
    std::string patch;
    if (const auto patch_mpq = find_named(dest, "patch_d2.mpq")) patch = patch_mpq->string();
    else if (const auto installer = find_named(dest, "LODPatch_114d.exe")) patch = installer->string();
    if (!d2d::userdir::save_cfg(cfg_file, { { "data", dest.string() }, { "patch", patch } })) {
        d2d::log::error("Could not write {}", cfg_file.string());
        return false;
    }
    return true;
}

}  // namespace

bool d2d::ios::adopt_game_data_if_present(const fs::path& dest, const fs::path& cfg_file) {
    if (!write_data_cfg(dest, cfg_file)) return false;
    d2d::log::info("Using game data in {} (d2d.cfg updated).", dest.string());
    return true;
}

bool d2d::ios::import_game_data(const fs::path& dest, const fs::path& cfg_file) {
    d2d::log::info("Pick the folder that contains d2data.mpq.");
    NSURL* url = pick_folder();
    if (!url) {
        d2d::log::warn("No folder picked.");
        return false;
    }
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    std::error_code error;
    fs::create_directories(dest.parent_path(), error);
    if (error) {
        if (scoped) [url stopAccessingSecurityScopedResource];
        d2d::log::error("Could not create the game data directory: {}", error.message());
        return false;
    }
    // Unique siblings avoid deleting a previous interrupted import or backup.
    const std::string suffix = NSUUID.UUID.UUIDString.UTF8String;
    const fs::path staging = dest.parent_path() / (dest.filename().string() + ".importing-" + suffix);
    const fs::path backup = dest.parent_path() / (dest.filename().string() + ".backup-" + suffix);
    NSString* staging_text = [NSString stringWithUTF8String:staging.string().c_str()];
    NSURL* staging_url = [NSURL fileURLWithPath:staging_text isDirectory:YES];
    NSError* copy_error = nil;
    const BOOL copied = [[NSFileManager defaultManager] copyItemAtURL:url toURL:staging_url error:&copy_error];
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (!copied || !find_named(staging, "d2data.mpq")) {
        fs::remove_all(staging, error);
        d2d::log::error("Could not copy valid game data from the picked folder.");
        return false;
    }
    const bool had_old_data = fs::exists(dest, error);
    if (error) {
        fs::remove_all(staging, error);
        d2d::log::error("Could not inspect the existing game data directory.");
        return false;
    }
    if (had_old_data) fs::rename(dest, backup, error);
    if (error) {
        fs::remove_all(staging, error);
        d2d::log::error("Could not back up the existing game data.");
        return false;
    }
    fs::rename(staging, dest, error);
    if (error) {
        std::error_code cleanup_error;
        fs::remove_all(staging, cleanup_error);
        if (had_old_data) fs::rename(backup, dest, cleanup_error);
        if (cleanup_error) d2d::log::error("Previous game data remains at {}.", backup.string());
        d2d::log::error("Could not install the copied game data: {}", error.message());
        return false;
    }
    if (!write_data_cfg(dest, cfg_file)) {
        // Keep both copies if rollback fails; never remove the old backup.
        fs::remove_all(dest, error);
        if (had_old_data && !error) fs::rename(backup, dest, error);
        if (error && had_old_data) d2d::log::error("Previous game data remains at {}.", backup.string());
        d2d::log::error("Could not write {} after importing game data.", cfg_file.string());
        return false;
    }
    if (had_old_data) {
        fs::remove_all(backup, error);
        if (error) d2d::log::warn("Could not remove game data backup at {}.", backup.string());
    }
    d2d::log::info("Imported game data into {}", dest.string());
    return true;
}
