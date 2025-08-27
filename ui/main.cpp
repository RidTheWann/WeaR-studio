#include "WeaRApp.h"
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    WeaRApp app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
