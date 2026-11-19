#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <array>

namespace {
QFrame *dayCell(int level, QWidget *parent)
{
    static const std::array<const char *, 5> colours = {
        "#ebedf0", "#9be9a8", "#40c463", "#30a14e", "#216e39"
    };

    auto *cell = new QFrame(parent);
    cell->setFixedSize(13, 13);
    cell->setStyleSheet(QString("background: %1; border-radius: 3px;")
                            .arg(colours.at(static_cast<std::size_t>(level))));
    cell->setToolTip(level == 0 ? "No commits. A peaceful day."
                                : QString("%1 contribution(s).").arg(level * 3));
    return cell;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("GitHub Activity (Trust Me)");
    window.resize(850, 440);
    window.setMinimumSize(680, 390);

    auto *central = new QWidget(&window);
    central->setStyleSheet("background: #ffffff; color: #1f2328;");
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(34, 28, 34, 26);
    layout->setSpacing(14);

    auto *top = new QHBoxLayout();
    auto *heading = new QLabel("My GitHub activity");
    QFont headingFont = heading->font();
    headingFont.setPointSize(21);
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto *count = new QLabel("3 contributions in the last year");
    count->setStyleSheet("color: #656d76; font-size: 13px;");
    top->addWidget(heading);
    top->addStretch();
    top->addWidget(count);
    layout->addLayout(top);

    auto *subtitle = new QLabel("A detailed record of my dedication to pushing code (and occasionally fixing typos).");
    subtitle->setStyleSheet("color: #656d76; font-size: 13px;");
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    auto *chartCard = new QFrame();
    chartCard->setStyleSheet("QFrame { background: #ffffff; border: 1px solid #d1d9e0; border-radius: 9px; }");
    auto *chartLayout = new QVBoxLayout(chartCard);
    chartLayout->setContentsMargins(18, 16, 18, 14);
    chartLayout->setSpacing(10);

    auto *months = new QHBoxLayout();
    months->setContentsMargins(34, 0, 0, 0);
    const std::array<const char *, 6> monthNames = {"Jan", "Mar", "May", "Jul", "Sep", "Nov"};
    for (const char *month : monthNames) {
        auto *label = new QLabel(month);
        label->setStyleSheet("color: #656d76; font-size: 11px; border: none;");
        months->addWidget(label);
        months->addStretch(1);
    }
    chartLayout->addLayout(months);

    auto *graph = new QGridLayout();
    graph->setContentsMargins(0, 0, 0, 0);
    graph->setHorizontalSpacing(4);
    graph->setVerticalSpacing(4);

    const std::array<const char *, 7> weekdays = {"Mon", "", "Wed", "", "Fri", "", ""};
    for (int row = 0; row < 7; ++row) {
        auto *label = new QLabel(weekdays[static_cast<std::size_t>(row)]);
        label->setFixedWidth(28);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setStyleSheet("color: #656d76; font-size: 10px; border: none;");
        graph->addWidget(label, row, 0);
    }

    // A deliberately suspicious pattern: activity peaks right before deadlines.
    for (int week = 0; week < 26; ++week) {
        for (int day = 0; day < 7; ++day) {
            int level = 0;
            if ((week == 3 && day == 2) || (week == 11 && day == 4) ||
                (week == 18 && day == 1) || (week == 24 && day == 3)) {
                level = 4;
            } else if ((week == 2 && day == 3) || (week == 10 && day == 4) ||
                       (week == 17 && day == 2) || (week == 23 && day == 4)) {
                level = 2;
            } else if ((week == 3 && day == 3) || (week == 18 && day == 2)) {
                level = 1;
            }
            graph->addWidget(dayCell(level, chartCard), day, week + 1);
        }
    }
    chartLayout->addLayout(graph);

    auto *legend = new QHBoxLayout();
    legend->addStretch();
    auto *less = new QLabel("Less");
    less->setStyleSheet("color: #656d76; font-size: 10px; border: none;");
    legend->addWidget(less);
    for (int level = 0; level < 5; ++level)
        legend->addWidget(dayCell(level, chartCard));
    auto *more = new QLabel("More");
    more->setStyleSheet("color: #656d76; font-size: 10px; border: none;");
    legend->addWidget(more);
    chartLayout->addLayout(legend);

    layout->addWidget(chartCard);

    auto *joke = new QLabel("Turns out my most productive day is ‘the day before it’s due’.\nConsistency is temporary. The green squares are forever.");
    joke->setWordWrap(true);
    joke->setStyleSheet("background: #f6f8fa; border: 1px solid #d1d9e0; border-radius: 8px;"
                        "padding: 13px; color: #57606a; font-size: 13px;");
    layout->addWidget(joke);

    auto *footer = new QHBoxLayout();
    auto *refresh = new QPushButton("Reassess my life choices");
    refresh->setCursor(Qt::PointingHandCursor);
    refresh->setStyleSheet("QPushButton { background: #238636; color: white; border: 0;"
                           "border-radius: 6px; padding: 9px 14px; font-weight: 600; }"
                           "QPushButton:hover { background: #2ea043; }");
    auto *close = new QPushButton("Close");
    close->setStyleSheet("QPushButton { background: #f6f8fa; color: #24292f; border: 1px solid #d1d9e0;"
                         "border-radius: 6px; padding: 9px 14px; }");
    footer->addWidget(refresh);
    footer->addStretch();
    footer->addWidget(close);
    layout->addLayout(footer);

    QObject::connect(refresh, &QPushButton::clicked, joke, [joke]() {
        joke->setText("Refreshing activity…\nStill waiting for the motivation commit to land.");
    });
    QObject::connect(close, &QPushButton::clicked, &window, &QMainWindow::close);

    window.setCentralWidget(central);
    window.show();
    return app.exec();
}
