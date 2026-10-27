// Scenario tests for tools/shell-extras/ICloudSaveSyncCore.mm, built and run by tests/test_tools.py.
// Usage: icloud_sync_harness <scratch-dir>; prints "ok <scenario>" per scenario, exits 1 on failure.
#import <Foundation/Foundation.h>
#import "ICloudSaveSyncCore.h"

static NSURL *gLocal, *gCloud, *gManifest;
static int gFailures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __func__, __LINE__, #cond); gFailures++; } } while (0)

static void Put(NSURL* dir, NSString* name, NSString* text, NSTimeInterval ago)
{
    NSURL* u = [dir URLByAppendingPathComponent:name];
    [[text dataUsingEncoding:NSUTF8StringEncoding] writeToURL:u atomically:YES];
    [[NSFileManager defaultManager] setAttributes:@{NSFileModificationDate: [NSDate dateWithTimeIntervalSinceNow:-ago]} ofItemAtPath:u.path error:nil];
}

static NSString* Get(NSURL* dir, NSString* name)
{
    NSData* d = [NSData dataWithContentsOfURL:[dir URLByAppendingPathComponent:name]];
    return d ? [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] : nil;
}

static NSDictionary<NSString*, NSNumber*>* Sync(ManosabaSyncMode mode)
{
    return ManosabaSyncSaves(gLocal, gCloud, gManifest, mode, 0, nil, nil);
}

static void Reset(NSURL* root)
{
    NSFileManager* fm = [NSFileManager defaultManager];
    [fm removeItemAtURL:root error:nil];
    gLocal = [root URLByAppendingPathComponent:@"local/Saves_v1"];
    gCloud = [root URLByAppendingPathComponent:@"cloud/Saves_v1"];
    gManifest = [root URLByAppendingPathComponent:@"support/manifest.json"];
    [fm createDirectoryAtURL:gLocal withIntermediateDirectories:YES attributes:nil error:nil];
    [fm createDirectoryAtURL:gCloud withIntermediateDirectories:YES attributes:nil error:nil];
}

static void FirstSyncUploadsEverything(void)
{
    Put(gLocal, @"GameSave001.nson", @"a", 100);
    Put(gLocal, @"GlobalSave.nson", @"g", 100);
    Put(gLocal, @"Settings.json", @"per-device", 100);
    NSDictionary* s = Sync(ManosabaSyncModeFull);
    CHECK([s[@"pushed"] intValue] == 2);
    CHECK([Get(gCloud, @"GameSave001.nson") isEqualToString:@"a"]);
    CHECK(Get(gCloud, @"Settings.json") == nil);  // settings stay per device
    s = Sync(ManosabaSyncModeFull);
    CHECK([s[@"pushed"] intValue] == 0 && [s[@"pulled"] intValue] == 0);
}

static void PushOnlyUploadsButNeverPulls(void)
{
    FirstSyncUploadsEverything();
    Put(gLocal, @"GameSave001.nson", @"a2", 50);
    Put(gCloud, @"GlobalSave.nson", @"g-other-device", 50);
    NSDictionary* s = Sync(ManosabaSyncModePushOnly);
    CHECK([s[@"pushed"] intValue] == 1 && [s[@"pulled"] intValue] == 0);
    CHECK([Get(gCloud, @"GameSave001.nson") isEqualToString:@"a2"]);
    CHECK([Get(gLocal, @"GlobalSave.nson") isEqualToString:@"g"]);
    s = Sync(ManosabaSyncModeFull);
    CHECK([s[@"pulled"] intValue] == 1);
    CHECK([Get(gLocal, @"GlobalSave.nson") isEqualToString:@"g-other-device"]);
}

static void NewCloudFileIsPulled(void)
{
    FirstSyncUploadsEverything();
    Put(gCloud, @"GameAutoSave260715010105922.nson", @"auto", 10);
    NSDictionary* s = Sync(ManosabaSyncModeFull);
    CHECK([s[@"pulled"] intValue] == 1);
    CHECK([Get(gLocal, @"GameAutoSave260715010105922.nson") isEqualToString:@"auto"]);
}

static void LocalDeletionPropagates(void)
{
    FirstSyncUploadsEverything();
    [[NSFileManager defaultManager] removeItemAtURL:[gLocal URLByAppendingPathComponent:@"GameSave001.nson"] error:nil];
    NSDictionary* s = Sync(ManosabaSyncModePushOnly);
    CHECK([s[@"deletedCloud"] intValue] == 1);
    CHECK(Get(gCloud, @"GameSave001.nson") == nil);
}

static void CloudDeletionMovesLocalToTrash(void)
{
    FirstSyncUploadsEverything();
    [[NSFileManager defaultManager] removeItemAtURL:[gCloud URLByAppendingPathComponent:@"GameSave001.nson"] error:nil];
    NSDictionary* s = Sync(ManosabaSyncModeFull);
    CHECK([s[@"deletedLocal"] intValue] == 1);
    CHECK(Get(gLocal, @"GameSave001.nson") == nil);
    NSURL* trash = [gManifest.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"manosaba-icloud-trash"];
    NSArray* kept = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:trash.path error:nil];
    CHECK(kept.count == 1 && [kept.firstObject hasSuffix:@"-GameSave001.nson"]);
}

static void ConflictNewerEditWins(void)
{
    FirstSyncUploadsEverything();
    Put(gLocal, @"GlobalSave.nson", @"local-older", 30);
    Put(gCloud, @"GlobalSave.nson", @"cloud-newer", 5);
    Put(gLocal, @"GameSave001.nson", @"local-newer", 5);
    Put(gCloud, @"GameSave001.nson", @"cloud-older", 30);
    NSDictionary* s = Sync(ManosabaSyncModeFull);
    CHECK([s[@"conflicts"] intValue] == 2);
    CHECK([Get(gLocal, @"GlobalSave.nson") isEqualToString:@"cloud-newer"]);
    CHECK([Get(gCloud, @"GameSave001.nson") isEqualToString:@"local-newer"]);
}

static NSArray<NSString*>* Trash(void)
{
    NSURL* trash = [gManifest.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"manosaba-icloud-trash"];
    return [[[NSFileManager defaultManager] contentsOfDirectoryAtPath:trash.path error:nil] sortedArrayUsingSelector:@selector(compare:)];
}

static void ConflictLoserIsKeptInTrash(void)
{
    FirstSyncUploadsEverything();
    Put(gLocal, @"GlobalSave.nson", @"local-older", 30);
    Put(gCloud, @"GlobalSave.nson", @"cloud-newer", 5);
    Sync(ManosabaSyncModeFull);
    NSArray* kept = Trash();
    CHECK(kept.count == 1 && [kept.firstObject hasSuffix:@"-GlobalSave.nson"]);
}

static void FirstSyncOnNewDevicePrefersCloud(void)
{
    // iCloud already holds the real saves; this device has newer *test* saves of the same names.
    Put(gCloud, @"GameSave001.nson", @"real", 3600);
    Put(gCloud, @"GlobalSave.nson", @"real-global", 3600);
    Put(gLocal, @"GameSave001.nson", @"test", 5);
    Put(gLocal, @"GlobalSave.nson", @"test-global", 5);
    Put(gLocal, @"GameAutoSave261026185037218.nson", @"test-auto", 5);
    NSDictionary* s = Sync(ManosabaSyncModeFull);
    CHECK([s[@"conflicts"] intValue] == 2 && [s[@"pulled"] intValue] == 2);
    CHECK([Get(gLocal, @"GameSave001.nson") isEqualToString:@"real"]);
    CHECK([Get(gCloud, @"GlobalSave.nson") isEqualToString:@"real-global"]);
    CHECK(Trash().count == 2);
    CHECK([Get(gCloud, @"GameAutoSave261026185037218.nson") isEqualToString:@"test-auto"]);  // new names still upload
}

static void EmptiedCloudIsRefilledNotMirrored(void)
{
    FirstSyncUploadsEverything();
    NSFileManager* fm = [NSFileManager defaultManager];
    [fm removeItemAtURL:gCloud error:nil];
    NSDictionary* s = Sync(ManosabaSyncModeFull);
    CHECK([s[@"deletedLocal"] intValue] == 0);
    CHECK([s[@"pushed"] intValue] == 2);
    CHECK([Get(gLocal, @"GameSave001.nson") isEqualToString:@"a"]);
}

static void PlaceholderIsSkipped(void)
{
    FirstSyncUploadsEverything();
    [[NSFileManager defaultManager] removeItemAtURL:[gCloud URLByAppendingPathComponent:@"GameSave001.nson"] error:nil];
    Put(gCloud, @".GameSave001.nson.icloud", @"placeholder", 5);
    Put(gCloud, @".GameSave002.nson.icloud", @"placeholder", 5);
    NSDictionary* s = Sync(ManosabaSyncModeFull);
    CHECK([s[@"skipped"] intValue] == 2 && [s[@"deletedLocal"] intValue] == 0);
    CHECK([Get(gLocal, @"GameSave001.nson") isEqualToString:@"a"]);
    CHECK(Get(gLocal, @"GameSave002.nson") == nil);
}

static void FileBeingWrittenIsSkipped(void)
{
    FirstSyncUploadsEverything();
    Put(gLocal, @"GameQuickSave001.nson", @"half-written", 0);
    NSDictionary* s = ManosabaSyncSaves(gLocal, gCloud, gManifest, ManosabaSyncModePushOnly, 5, nil, nil);
    CHECK([s[@"skipped"] intValue] == 1 && [s[@"pushed"] intValue] == 0);
}

static void PullStopsWhenNotAllowed(void)
{
    FirstSyncUploadsEverything();
    Put(gCloud, @"GameSave009.nson", @"late", 5);
    NSDictionary* s = ManosabaSyncSaves(gLocal, gCloud, gManifest, ManosabaSyncModeFull, 0, nil, ^BOOL { return NO; });
    CHECK([s[@"pulled"] intValue] == 0 && [s[@"skipped"] intValue] == 1);
    CHECK(Get(gLocal, @"GameSave009.nson") == nil);
}

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 2) { printf("usage: %s <scratch-dir>\n", argv[0]); return 2; }
        NSURL* root = [NSURL fileURLWithPath:@(argv[1]) isDirectory:YES];
        struct { const char* name; void (*fn)(void); } scenarios[] = {
            {"first_sync_uploads_everything", FirstSyncUploadsEverything},
            {"push_only_uploads_but_never_pulls", PushOnlyUploadsButNeverPulls},
            {"new_cloud_file_is_pulled", NewCloudFileIsPulled},
            {"local_deletion_propagates", LocalDeletionPropagates},
            {"cloud_deletion_moves_local_to_trash", CloudDeletionMovesLocalToTrash},
            {"conflict_newer_edit_wins", ConflictNewerEditWins},
            {"conflict_loser_is_kept_in_trash", ConflictLoserIsKeptInTrash},
            {"first_sync_on_new_device_prefers_cloud", FirstSyncOnNewDevicePrefersCloud},
            {"emptied_cloud_is_refilled_not_mirrored", EmptiedCloudIsRefilledNotMirrored},
            {"placeholder_is_skipped", PlaceholderIsSkipped},
            {"file_being_written_is_skipped", FileBeingWrittenIsSkipped},
            {"pull_stops_when_not_allowed", PullStopsWhenNotAllowed},
        };
        for (auto& sc : scenarios)
        {
            int before = gFailures;
            Reset([root URLByAppendingPathComponent:@(sc.name)]);
            sc.fn();
            printf("%s %s\n", gFailures == before ? "ok" : "FAILED", sc.name);
        }
        return gFailures ? 1 : 0;
    }
}
