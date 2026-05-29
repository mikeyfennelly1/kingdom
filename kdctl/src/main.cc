#include "LoginWindow.hh"
#include "MainWindow.hh"

#include <QApplication>
#include <functional>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setApplicationName("Kingdom");
  app.setApplicationVersion("1.0");

  // showLogin is recursive — it re-displays the login dialog after logout.
  std::function<void()> showLogin;
  showLogin = [&showLogin]() {
    auto* login = new LoginWindow();
    login->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(login, &QDialog::accepted, [login, &showLogin]() {
      auto res = login->result();
      if (!res.has_value()) {
        showLogin();
        return;
      }

      MainWindow::Session session;
      session.userId = res->userId;
      session.username = res->username;
      session.token = res->token;
      session.identityKey = std::move(res->identityKey);
      session.serverUrl = res->serverUrl;

      auto* mainWin = new MainWindow(std::move(session));
      mainWin->setAttribute(Qt::WA_DeleteOnClose);

      QObject::connect(mainWin, &MainWindow::loggedOut, [&showLogin]() { showLogin(); });

      mainWin->show();
    });

    QObject::connect(login, &QDialog::rejected, []() { QApplication::quit(); });

    login->show();
  };

  showLogin();
  return QApplication::exec();
}
