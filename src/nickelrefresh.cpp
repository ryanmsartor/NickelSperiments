#include <NickelHook.h>
#include <fbink.h>

#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QIODevice>
#include <QTimer>

namespace {

const char kStartPath[] = "/mnt/onboard/.kobo/NickelRefresh.start";
const char kLogPath[] = "/mnt/onboard/.kobo/NickelRefresh.csv";
const qint64 kStepDurationMs = 3000;
const unsigned short kPatchSize = 160;
const unsigned int kRates[] = {1, 2, 4, 8, 12, 16, 20};

struct Waveform {
    const char* name;
    WFM_MODE_INDEX_T mode;
};

const Waveform kWaveforms[] = {
    {"A2", WFM_A2},
    {"DU", WFM_DU},
    {"GC16", WFM_GC16},
};

struct TestState {
    QTimer* timer;
    QElapsedTimer elapsed;
    FBInkConfig config;
    FBInkDump savedPatch;
    FBInkRect patch;
    int fbfd;
    int waveformIndex;
    int rateIndex;
    unsigned int sequence;
    qint64 stepStartMs;
    qint64 nextRequestMs;
    QByteArray stepLog;
    bool black;
};

TestState* g_test = nullptr;

struct nh_info info = {
    .name = "NickelRefresh",
    .desc = "Opt-in Kobo e-paper refresh-rate test",
    .uninstall_flag = "/mnt/onboard/.kobo/NickelRefresh.uninstall",
};

struct nh_hook hooks[] = {{0}};
struct nh_dlsym symbols[] = {{0}};

bool appendCsv(const QByteArray& row)
{
    QFile file{QLatin1String(kLogPath)};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        nh_log("Could not open %s: %s", kLogPath, qPrintable(file.errorString()));
        return false;
    }

    const qint64 written = file.write(row);
    file.close();
    if (written != row.size()) {
        nh_log("Could not write complete CSV row to %s", kLogPath);
        return false;
    }
    return true;
}

void releaseFramebuffer()
{
    if (!g_test) {
        return;
    }

    g_test->config.wfm_mode = WFM_GC16;
    if (g_test->savedPatch.data) {
        const int restoreResult = fbink_restore(g_test->fbfd, &g_test->config, &g_test->savedPatch);
        if (restoreResult < 0) {
            nh_log("Framebuffer restore failed: %d", restoreResult);
        }
        const int freeResult = fbink_free_dump_data(&g_test->savedPatch);
        if (freeResult < 0) {
            nh_log("Could not free framebuffer backup: %d", freeResult);
        }
    }
    if (g_test->fbfd >= 0) {
        const int closeResult = fbink_close(g_test->fbfd);
        if (closeResult < 0) {
            nh_log("Could not close FBInk: %d", closeResult);
        }
        g_test->fbfd = -1;
    }
}

bool flushStepLog()
{
    if (!g_test || g_test->stepLog.isEmpty()) {
        return true;
    }

    if (!appendCsv(g_test->stepLog)) {
        return false;
    }
    g_test->stepLog.clear();
    return true;
}

void finishTest(const char* reason)
{
    if (!g_test) {
        return;
    }

    g_test->timer->stop();
    if (!flushStepLog()) {
        nh_log("Could not flush final measurements");
    }
    if (!appendCsv(QByteArray("end,") +
                   QByteArray::number(QDateTime::currentMSecsSinceEpoch()) +
                   ",,,,,," + reason + '\n')) {
        nh_log("Could not write test completion record");
    }
    releaseFramebuffer();
    delete g_test->timer;
    delete g_test;
    g_test = nullptr;
    nh_log("Test finished: %s", reason);
}

void advanceStep()
{
    if (!flushStepLog()) {
        finishTest("log-error");
        return;
    }

    ++g_test->rateIndex;
    g_test->sequence = 0;
    g_test->nextRequestMs = 0;
    if (g_test->rateIndex < static_cast<int>(sizeof(kRates) / sizeof(kRates[0]))) {
        g_test->stepStartMs += kStepDurationMs;
        return;
    }

    g_test->rateIndex = 0;
    ++g_test->waveformIndex;
    if (g_test->waveformIndex >= static_cast<int>(sizeof(kWaveforms) / sizeof(kWaveforms[0]))) {
        finishTest("complete");
        return;
    }

    g_test->config.wfm_mode = kWaveforms[g_test->waveformIndex].mode;
    g_test->stepStartMs += kStepDurationMs;
}

void tick()
{
    if (!g_test) {
        return;
    }

    while (g_test->elapsed.elapsed() - g_test->stepStartMs >= kStepDurationMs) {
        advanceStep();
        if (!g_test) {
            return;
        }
    }

    const qint64 elapsedInStep = g_test->elapsed.elapsed() - g_test->stepStartMs;
    const unsigned int targetRate = kRates[g_test->rateIndex];
    const qint64 intervalMs = 1000 / targetRate;
    if (elapsedInStep < g_test->nextRequestMs) {
        return;
    }

    const qint64 startMs = g_test->elapsed.elapsed();
    const qint64 wallTimeMs = QDateTime::currentMSecsSinceEpoch();
    const int result = fbink_fill_rect_gray(g_test->fbfd,
                                            &g_test->config,
                                            &g_test->patch,
                                            false,
                                            g_test->black ? 0 : 255);
    const qint64 callDurationMs = g_test->elapsed.elapsed() - startMs;
    g_test->stepLog += QByteArray("update,") +
                       QByteArray::number(wallTimeMs) + ',' +
                       kWaveforms[g_test->waveformIndex].name + ',' +
                       QByteArray::number(targetRate) + ',' +
                       QByteArray::number(g_test->sequence) + ',' +
                       QByteArray::number(startMs - g_test->stepStartMs) + ',' +
                       QByteArray::number(callDurationMs) + ',' +
                       QByteArray::number(result) + '\n';
    if (result < 0) {
        nh_log("FBInk update failed: %d", result);
        finishTest("fbink-error");
        return;
    }

    g_test->black = !g_test->black;
    ++g_test->sequence;
    g_test->nextRequestMs = g_test->elapsed.elapsed() - g_test->stepStartMs + intervalMs;
}

int startTest()
{
    if (!QFile::exists(QLatin1String(kStartPath))) {
        return 0;
    }
    if (!QFile::remove(QLatin1String(kStartPath))) {
        nh_log("Could not consume start file %s", kStartPath);
        return 1;
    }
    if (!QCoreApplication::instance()) {
        nh_log("No Qt application instance is available");
        return 1;
    }

    g_test = new TestState();
    g_test->timer = nullptr;
    g_test->fbfd = -1;
    g_test->savedPatch = FBInkDump();
    g_test->config = FBInkConfig();
    g_test->config.is_quiet = true;
    g_test->config.wfm_mode = kWaveforms[0].mode;

    g_test->fbfd = fbink_open();
    if (g_test->fbfd < 0) {
        nh_log("Could not open the framebuffer: %d", g_test->fbfd);
        delete g_test;
        g_test = nullptr;
        return 1;
    }

    const int initResult = fbink_init(g_test->fbfd, &g_test->config);
    if (initResult < 0) {
        nh_log("FBInk initialization failed: %d", initResult);
        fbink_close(g_test->fbfd);
        g_test->fbfd = -1;
        delete g_test;
        g_test = nullptr;
        return 1;
    }

    FBInkState state = {};
    fbink_get_state(&g_test->config, &state);
    if (state.screen_width < kPatchSize || state.screen_height < kPatchSize) {
        nh_log("Framebuffer is too small for the test patch");
        releaseFramebuffer();
        delete g_test;
        g_test = nullptr;
        return 1;
    }

    g_test->patch.left = static_cast<unsigned short>((state.screen_width - kPatchSize) / 2);
    g_test->patch.top = static_cast<unsigned short>((state.screen_height - kPatchSize) / 2);
    g_test->patch.width = kPatchSize;
    g_test->patch.height = kPatchSize;
    const int dumpResult = fbink_rect_dump(g_test->fbfd, &g_test->patch, &g_test->savedPatch);
    if (dumpResult < 0) {
        nh_log("Could not save framebuffer patch: %d", dumpResult);
        releaseFramebuffer();
        delete g_test;
        g_test = nullptr;
        return 1;
    }

    QFile log{QLatin1String(kLogPath)};
    if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        nh_log("Could not create %s: %s", kLogPath, qPrintable(log.errorString()));
        releaseFramebuffer();
        delete g_test;
        g_test = nullptr;
        return 1;
    }
    const QByteArray header = "event,timestamp_ms,waveform,target_hz,sequence,step_elapsed_ms,call_ms,result\n";
    if (log.write(header) != header.size()) {
        nh_log("Could not write CSV header to %s", kLogPath);
        log.close();
        releaseFramebuffer();
        delete g_test;
        g_test = nullptr;
        return 1;
    }
    log.close();

    g_test->waveformIndex = 0;
    g_test->rateIndex = 0;
    g_test->sequence = 0;
    g_test->stepStartMs = 0;
    g_test->nextRequestMs = 0;
    g_test->black = false;
    g_test->elapsed.start();
    g_test->timer = new QTimer(QCoreApplication::instance());
    g_test->timer->setTimerType(Qt::PreciseTimer);
    g_test->timer->setInterval(5);
    QObject::connect(g_test->timer, &QTimer::timeout, g_test->timer, []() { tick(); });
    g_test->timer->start();
    nh_log("Started refresh sweep; results: %s", kLogPath);
    return 0;
}

} // namespace

NickelHook(
    .init = startTest,
    .info = &info,
    .hook = hooks,
    .dlsym = symbols,
)
