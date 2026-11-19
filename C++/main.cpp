#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("My C++ App");
    window.resize(720, 460);
    window.setMinimumSize(520, 340);

    auto *central = new QWidget(&window);
    central->setStyleSheet("background-color: #f4f6fb; color: #172033;");
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(48, 42, 48, 42);
    layout->setSpacing(18);

    auto *eyebrow = new QLabel("C++ STARTER PROJECT");
    eyebrow->setStyleSheet("color: #5265d8; font-size: 12px; font-weight: 700; letter-spacing: 2px;");

    auto *heading = new QLabel("Welcome.");
    QFont headingFont = heading->font();
    headingFont.setPointSize(30);
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto *description = new QLabel(
        "Your new C++ desktop application is up and running.\n"
        "Use this screen as a starting point for your next idea."
    );
    description->setWordWrap(true);
    description->setStyleSheet("color: #596579; font-size: 15px; line-height: 1.5;");

    auto *status = new QLabel("Ready when you are.");
    status->setStyleSheet(
        "background: white; border: 1px solid #e1e6f0; border-radius: 10px;"
        "padding: 16px; color: #39445a; font-size: 14px;"
    );

    auto *actions = new QHBoxLayout();
    auto *helloButton = new QPushButton("Say hello");
    helloButton->setCursor(Qt::PointingHandCursor);
    helloButton->setStyleSheet(
        "QPushButton { background: #5265d8; color: white; border: 0;"
        "border-radius: 8px; padding: 11px 20px; font-weight: 600; }"
        "QPushButton:hover { background: #4052c2; }"
    );

    auto *closeButton = new QPushButton("Close");
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(
        "QPushButton { background: white; color: #39445a; border: 1px solid #d9dfeb;"
        "border-radius: 8px; padding: 11px 20px; }"
        "QPushButton:hover { background: #edf0f7; }"
    );

    actions->addWidget(helloButton);
    actions->addWidget(closeButton);
    actions->addStretch();

    layout->addWidget(eyebrow);
    layout->addSpacing(12);
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addSpacing(12);
    layout->addWidget(status);
    layout->addStretch();
    layout->addLayout(actions);

    QObject::connect(helloButton, &QPushButton::clicked, [&]() {
        status->setText("Hello! Your Qt application is responding to button clicks.");
    });
    QObject::connect(closeButton, &QPushButton::clicked, &window, &QMainWindow::close);

    window.setCentralWidget(central);
    window.show();

    return app.exec();
}
