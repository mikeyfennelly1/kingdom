#pragma once

#include <QDialog>
#include <QString>
#include <kd/LocalKeyStore.hpp>
#include <kd/MessageStore.hpp>
#include <optional>
#include <string>

class QLabel;
class QLineEdit;
class QPushButton;

class LoginWindow : public QDialog {
  Q_OBJECT

 public:
  explicit LoginWindow(QWidget* parent = nullptr);

  struct LoginResult {
    uint64_t userId;
    std::string username;
    std::string token;
    kd::LocalIdentityKey identityKey;
    kd::MessageStore messageStore;
    std::string serverUrl;
    std::string caCertPath;
  };

  [[nodiscard]] std::optional<LoginResult> takeResult() { return std::move(result_); }

 private slots:
  void onLogin();
  void onSignup();

 private:
  void performAuth(bool isSignup);
  void showError(const QString& msg);

  QLineEdit* serverUrlEdit_;
  QLineEdit* usernameEdit_;
  QLineEdit* passwordEdit_;
  QPushButton* loginButton_;
  QPushButton* signupButton_;
  QLabel* errorLabel_;

  std::optional<LoginResult> result_;
};
