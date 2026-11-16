/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <NickelHook.h>

#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QTouchEvent>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace {

const char kPidPath[] = "/tmp/koboy.pid";
const char kProgramRoot[] = "/mnt/onboard/.adds/koboy";
const char kSupervisorPath[] = "/mnt/onboard/.adds/koboy/koboy-supervisor";
const char kLogPath[] = "/mnt/onboard/.adds/koboy/koboy.log";
pid_t launchedSupervisorPid = -1;

typedef void (*LifecycleMethod)(void *self);
LifecycleMethod originalAboutToSleep = nullptr;
LifecycleMethod originalResumedFromSleep = nullptr;

bool readKoBoyPid(pid_t &pid)
{
    const int descriptor = open(kPidPath, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0)
        return false;
    char text[32] = { 0 };
    const ssize_t length = read(descriptor, text, sizeof(text) - 1U);
    close(descriptor);
    if (length <= 0)
        return false;
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &end, 10);
    if (errno || end == text || (*end != '\0' && *end != '\n') || parsed <= 1)
        return false;
    pid = static_cast<pid_t>(parsed);
    return true;
}

bool koBoyRunning()
{
    pid_t pid = -1;
    if (readKoBoyPid(pid) && (kill(pid, 0) == 0 || errno == EPERM))
        return true;
    if (launchedSupervisorPid > 1 &&
        (kill(launchedSupervisorPid, 0) == 0 || errno == EPERM))
        return true;
    launchedSupervisorPid = -1;
    return false;
}

bool launchKoBoy()
{
    if (koBoyRunning()) {
        nh_log("Ko Boy is already running");
        return true;
    }

    QStringList arguments;
    arguments << "-c"
              << QString("exec %1 >>%2 2>&1")
                     .arg(QString::fromLatin1(kSupervisorPath),
                          QString::fromLatin1(kLogPath));
    qint64 processId = 0;
    if (!QProcess::startDetached("/bin/sh", arguments, QString(), &processId)) {
        nh_log("Could not launch Ko Boy supervisor");
        return false;
    }
    launchedSupervisorPid = static_cast<pid_t>(processId);
    nh_log("Launched Ko Boy supervisor as pid %lld",
           static_cast<long long>(processId));
    return true;
}

// This row follows the same stable More-screen anchoring strategy as
// NickelModManager: it copies the stock Help row's geometry, font, palette,
// icon column, and pressed treatment without depending on its private class.
class KoBoyMenuEntry final : public QPushButton
{
  public:
    KoBoyMenuEntry(QWidget *reference, QWidget *parent)
        : QPushButton("Ko Boy", parent), reference_(reference)
    {
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setAttribute(Qt::WA_AcceptTouchEvents, true);
        if (reference)
            reference->installEventFilter(this);
    }

    QSize sizeHint() const override
    {
        const int height = reference_
            ? (reference_->height() > 1 ? reference_->height()
                                        : reference_->sizeHint().height())
            : fontMetrics().height() * 3;
        return QSize(QPushButton::sizeHint().width(), qMax(1, height));
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

  protected:
    bool event(QEvent *event) override
    {
        const auto touchInside = [this, event] {
            const QList<QTouchEvent::TouchPoint> points =
                static_cast<QTouchEvent *>(event)->touchPoints();
            return !points.isEmpty() &&
                   rect().contains(points.first().pos().toPoint());
        };
        switch (event->type()) {
            case QEvent::TouchBegin:
                if (!isEnabled())
                    return false;
                setDown(true);
                event->accept();
                return true;
            case QEvent::TouchUpdate:
                setDown(touchInside());
                event->accept();
                return true;
            case QEvent::TouchEnd: {
                const bool shouldClick = isEnabled() && touchInside();
                setDown(false);
                if (shouldClick)
                    click();
                event->accept();
                return true;
            }
            case QEvent::TouchCancel:
                setDown(false);
                event->accept();
                return true;
            default:
                return QPushButton::event(event);
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == reference_ &&
            (event->type() == QEvent::Resize ||
             event->type() == QEvent::Polish ||
             event->type() == QEvent::FontChange)) {
            updateGeometry();
            update();
        }
        return QPushButton::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QLabel *label = reference_
            ? reference_->findChild<QLabel *>("label") : nullptr;
        QLabel *glyph = reference_
            ? reference_->findChild<QLabel *>("pixmap") : nullptr;
        const QFont textFont = label ? label->font() : font();
        QColor ink = label
            ? label->palette().color(label->foregroundRole())
            : palette().color(QPalette::WindowText);
        const QColor paper = reference_
            ? reference_->palette().color(QPalette::Window)
            : palette().color(QPalette::Window);

        int left = qMax(16, height() / 4);
        if (label && label->isVisible()) {
            left = label->mapTo(reference_, label->contentsRect().topLeft()).x() +
                   qMax(0, label->indent());
        }
        if (isDown()) {
            painter.fillRect(rect(), ink);
            ink = paper;
        }

        if (glyph && glyph->isVisible()) {
            const QRect column(glyph->mapTo(reference_, QPoint()), glyph->size());
            const int side = qMax(1, qRound(qMin(column.width(), column.height()) * .6));
            const QRect icon(column.center().x() - side / 2,
                             column.center().y() - side / 2, side, side);
            const int stroke = qMax(2, side / 9);
            painter.setPen(QPen(ink, stroke, Qt::SolidLine, Qt::RoundCap,
                                Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            const int inset = stroke;
            painter.drawRoundedRect(icon.adjusted(inset, inset, -inset, -inset),
                                    side / 6, side / 6);
            painter.drawLine(icon.left() + side / 3, icon.center().y(),
                             icon.right() - side / 3, icon.center().y());
            painter.drawLine(icon.center().x(), icon.top() + side / 3,
                             icon.center().x(), icon.bottom() - side / 3);
        }

        painter.setFont(textFont);
        painter.setPen(ink);
        painter.drawText(QRect(left, 0, qMax(0, width() - left), height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text());
        painter.fillRect(QRect(0, height() - 1, width(), 1), ink);
    }

  private:
    QPointer<QWidget> reference_;
};

class KoBoyMoreIntegration final : public QObject
{
  public:
    explicit KoBoyMoreIntegration(QObject *parent) : QObject(parent)
    {
        qApp->installEventFilter(this);
        queueInspection();
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if ((type == QEvent::Polish || type == QEvent::Show) &&
            watched->isWidgetType() && watched->objectName() == "helpButton") {
            insertEntry(static_cast<QWidget *>(watched));
        }
        if (type == QEvent::Show || type == QEvent::ChildAdded ||
            type == QEvent::Polish)
            queueInspection();
        return QObject::eventFilter(watched, event);
    }

  private:
    bool inspectionQueued_ = false;

    void insertEntry(QWidget *help)
    {
        QWidget *parent = help->parentWidget();
        QBoxLayout *layout = parent
            ? qobject_cast<QBoxLayout *>(parent->layout()) : nullptr;
        if (!layout || layout->indexOf(help) < 0 ||
            parent->findChild<QPushButton *>("koBoyLauncherButton"))
            return;

        KoBoyMenuEntry *button = new KoBoyMenuEntry(help, parent);
        button->setObjectName("koBoyLauncherButton");
        layout->insertWidget(layout->indexOf(help), button);
        button->show();
        connect(button, &QPushButton::clicked, this, [] { launchKoBoy(); });
        nh_log("Added Ko Boy to Nickel's More screen");
    }

    void inspectWidgets()
    {
        const QWidgetList widgets = QApplication::allWidgets();
        for (QWidget *widget : widgets) {
            if (widget->objectName() == "helpButton")
                insertEntry(widget);
        }
    }

    void queueInspection()
    {
        if (inspectionQueued_)
            return;
        inspectionQueued_ = true;
        QTimer::singleShot(0, this, [this] {
            inspectionQueued_ = false;
            inspectWidgets();
        });
    }
};

int initializeKoBoyIntegration()
{
    if (!qApp) {
        nh_log("Ko Boy integration requires QApplication");
        return 1;
    }
    new KoBoyMoreIntegration(qApp);
    return 0;
}

void notifyKoBoy(int signalNumber, const char *event)
{
    pid_t pid = -1;
    if (!readKoBoyPid(pid))
        return;
    if (kill(pid, signalNumber) == 0)
        nh_log("Sent %s to Ko Boy pid %ld", event, static_cast<long>(pid));
    else if (errno != ESRCH)
        nh_log("Could not send %s to Ko Boy pid %ld: %s", event,
               static_cast<long>(pid), std::strerror(errno));
}

} // namespace

extern "C" __attribute__((visibility("default")))
void koboyAboutToSleep(void *self)
{
    notifyKoBoy(SIGUSR1, "sleep");
    originalAboutToSleep(self);
}

extern "C" __attribute__((visibility("default")))
void koboyResumedFromSleep(void *self)
{
    originalResumedFromSleep(self);
    notifyKoBoy(SIGUSR2, "wake");
}

bool uninstallKoBoyIntegration()
{
    const char *files[] = {
        "/mnt/onboard/.adds/koboy/koboy",
        "/mnt/onboard/.adds/koboy/koboy-supervisor",
        "/mnt/onboard/.adds/koboy/koboy.log",
        "/mnt/onboard/.adds/koboy/README.txt",
        "/mnt/onboard/.adds/koboy/uninstall-lifecycle-hook",
        "/mnt/onboard/.adds/koboy/LICENSES/EXPAT.txt",
        "/mnt/onboard/.adds/koboy/LICENSES/GPL-2.0.txt",
        "/mnt/onboard/.adds/koboy/LICENSES/KOBOY-LICENSES.md",
        "/mnt/onboard/.adds/koboy/LICENSES/MIT.txt",
        "/mnt/onboard/.adds/koboy/LICENSES/MPL-2.0.txt",
        "/mnt/onboard/.adds/koboy/LICENSES/PUBLIC-DOMAIN-NOTICES.txt",
        nullptr,
    };
    bool clean = true;
    for (const char **file = files; *file; ++file) {
        if (unlink(*file) != 0 && errno != ENOENT) {
            nh_log("Could not remove package file %s", *file);
            clean = false;
        }
    }
    if (rmdir("/mnt/onboard/.adds/koboy/LICENSES") != 0 && errno != ENOENT)
        clean = false;
    if (rmdir(kProgramRoot) != 0 && errno != ENOENT)
        clean = false;
    if (!clean)
        nh_log("Some Ko Boy program files could not be removed");
    nh_log("Removed Ko Boy program files; user content was preserved");
    return clean;
}

namespace {

struct nh_info info = {
    .name = "KoBoyIntegration",
    .desc = "Ko Boy launcher and sleep/wake integration",
    .uninstall_flag = "/mnt/onboard/.adds/koboy/uninstall",
};

struct nh_hook hooks[] = {
    {
        .sym = "_ZN22N3PowerWorkflowManager12aboutToSleepEv",
        .sym_new = "koboyAboutToSleep",
        .lib = "libnickel.so.1.0.0",
        .out = nh_symoutptr(originalAboutToSleep),
        .desc = "Pause Ko Boy when Nickel begins sleeping",
        .optional = true,
    },
    {
        .sym = "_ZN22N3PowerWorkflowManager16resumedFromSleepEv",
        .sym_new = "koboyResumedFromSleep",
        .lib = "libnickel.so.1.0.0",
        .out = nh_symoutptr(originalResumedFromSleep),
        .desc = "Resume Ko Boy when Nickel finishes waking",
        .optional = true,
    },
    {0},
};

struct nh_dlsym symbols[] = {{0}};

} // namespace

NickelHook(
    .init = initializeKoBoyIntegration,
    .info = &info,
    .hook = hooks,
    .dlsym = symbols,
    .uninstall = uninstallKoBoyIntegration,
)
