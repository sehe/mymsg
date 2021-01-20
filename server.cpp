#include<boost/asio.hpp>
#include<nlohmann/json.hpp>
#include<SQLAPI.h>
#include<spdlog/spdlog.h>


#include<atomic>
#include<iostream>
#include<fstream>
#include<memory>
#include<thread>
#include<sstream>

using json = nlohmann::json;
using namespace boost;

class Service {
public:
  Service(std::shared_ptr<asio::ip::tcp::socket> sock, std::shared_ptr<SAConnection> con):
    m_sock(sock),
    m_con(con) {
      m_sign_choice.reset(new asio::streambuf);
      m_login_buf.reset(new asio::streambuf);
      m_password_buf.reset(new asio::streambuf);
      m_action_choice.reset(new asio::streambuf);
    }

  void StartHandling() {
    asio::async_write(*m_sock.get(), asio::buffer("Sign in [1] or Sign up [2]: \n"),
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onSignUpRequestSent(ec, bytes_transferred); 
        });
  }

private:
  void onSignUpRequestSent(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onSignUpRequestSent, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 
    asio::async_read_until(*m_sock.get(), *(m_sign_choice.get()), '\n',
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onSignUpResponseReceived(ec, bytes_transferred);
        });
  }

  void onSignUpResponseReceived(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onSignUpResponseReceived, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 
    
    std::istream istrm(m_sign_choice.get());
    std::getline(istrm, sign_choice);
    m_sign_choice.reset(new asio::streambuf);

    if (sign_choice == "1" || sign_choice == "2") {
      asio::async_write(*m_sock.get(), asio::buffer("Enter login: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onLoginRequestSent(ec, bytes_transferred); 
          });
    } else {
      asio::async_write(*m_sock.get(), asio::buffer("Input incorrect. Enter 1 or 2: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onSignUpRequestSent(ec, bytes_transferred); 
          });
    } 
  }

  void onLoginRequestSent(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onLoginRequestSent, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 

    spdlog::info("Login request sent");
    asio::async_read_until(*m_sock.get(), *(m_login_buf.get()), '\n',
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onLoginReceived(ec, bytes_transferred);
        });
  } 

  void onLoginReceived(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onLoginReceived, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 
    spdlog::info("in onLoginReceived");
    std::istream istrm(m_login_buf.get());
    std::getline(istrm, login);
    m_login_buf.reset(new asio::streambuf);

    spdlog::info("Login received: {}", login);
    bool login_is_correct = true;

    SACommand select_log(m_con.get(), "select login from User where login = :1");
    select_log << _TSA(login.c_str());
    select_log.Execute();
    bool login_in_database = select_log.FetchNext();

    if (sign_choice == "2") {
      if (login_in_database) {
        login_is_correct = false;
        asio::async_write(*m_sock.get(), asio::buffer("Login is taken. Enter unique login: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onLoginRequestSent(ec, bytes_transferred); 
          });
      }
    } 
    if (sign_choice == "1"){
      if (!login_in_database) {
        login_is_correct = false;
        asio::async_write(*m_sock.get(), asio::buffer("No such login registered. Enter login: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onLoginRequestSent(ec, bytes_transferred); 
          });
      } 
    }  

    if (login_is_correct) {
      asio::async_write(*m_sock.get(), asio::buffer("Enter password: \n"),
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onPasswordRequestSent(ec, bytes_transferred); 
        });
    }
  } 

  void onPasswordRequestSent(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onPasswordRequestSent, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 
    spdlog::info("Password request sent");
    asio::async_read_until(*m_sock.get(), *(m_password_buf.get()), '\n',
      [this](const system::error_code& ec, std::size_t bytes_transferred) {
        onPasswordReceived(ec, bytes_transferred);
      });
  }

  bool password_is_valid(const std::string& password) const noexcept{
    return true;
  } 

  void onPasswordReceived(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onPasswordReceived, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    } 

    std::istream istrm(m_password_buf.get());
    std::string cur_password;
    std::getline(istrm, cur_password);
    m_password_buf.reset(new asio::streambuf);

    bool credentials_correct = true;
    if (password_is_valid(cur_password)) {
      if (sign_choice == "2") {
        if (password.empty()) {
          credentials_correct = false;
          password = cur_password;
          asio::async_write(*m_sock.get(), asio::buffer("Repeat password: \n"),
              [this](const system::error_code& ec, std::size_t bytes_transferred) {
                onPasswordRequestSent(ec, bytes_transferred); 
              });
        } else {
          if (password != cur_password) {
            credentials_correct = false;
            password = "";
            asio::async_write(*m_sock.get(), asio::buffer("Passwords do not match. Enter password: \n"),
                [this](const system::error_code& ec, std::size_t bytes_transferred) {
                  onPasswordRequestSent(ec, bytes_transferred); 
                });
          } else {
            credentials_correct = true;
          }  
        }  
      } else {
        password = cur_password;
        credentials_correct = true;
      }  

      spdlog::info("Password received: {}", password);
      if (credentials_correct) { 
        SACommand select_log_pw(m_con.get(), "select login, password from User where login = :1 and password = :2");
        select_log_pw << _TSA(login.c_str()) << _TSA(password.c_str());
        select_log_pw.Execute();
        
        bool credentials_registered = select_log_pw.FetchNext();
          
        if (credentials_registered) {
          if (sign_choice == "2") {
            SACommand insert(m_con.get(), "insert into User (login, password) values (:1, :2)");
            insert << _TSA(login.c_str()) << _TSA(password.c_str());
            insert.Execute();
          }
          asio::async_write(*m_sock.get(), asio::buffer("Welcome! \n"),
              [this](const system::error_code& ec, std::size_t bytes_transferred) {
                onAccountLogin(ec, bytes_transferred); 
              });
        } else {
          asio::async_write(*m_sock.get(), asio::buffer("Wrong login or password. Enter login: \n"),
              [this](const system::error_code& ec, std::size_t bytes_transferred) {
                onLoginRequestSent(ec, bytes_transferred); 
              });
        } 
      }
    } 
    else {
      password = "";
      asio::async_write(*m_sock.get(), asio::buffer("Invalid password. Enter good password: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onPasswordRequestSent(ec, bytes_transferred); 
          });
    } 
  }

  int _get_user_id(const std::string& user_login) const {
    SACommand select_user_id(m_con.get(), "select user_id from User where login = :1");
    select_user_id << _TSA(login.c_str());
    select_user_id.Execute();
    select_user_id.FetchNext();
    return select_user_id.Field("user_id").asLong();
  } 
  
  int _get_dialog_id(const std::string& user_login) const {
    int user_id = _get_user_id(login);
    SACommand select_dialog_id(m_con.get(), "select dialog_id from `User/Dialog` where user_id = :1");
    select_dialog_id << (long)user_id;
    select_dialog_id.Execute();
    select_dialog_id.FetchNext();
    return select_dialog_id.Field("dialog_id").asLong();
  } 
  
  std::string _get_login_sender(const int dialog_id) const {
    SACommand select_login_sender(m_con.get(),
        "select login from User where user_id=(select login_sender from Message where dialog_id = :1)");
    select_login_sender << (long)dialog_id;
    select_login_sender.Execute();
    select_login_sender.FetchNext();
    return select_login_sender.Field("login").asString().GetMultiByteChars();
  } 

  std::vector<std::vector<std::string>> _get_messages(const std::string& user_login) const {
    int dialog_id = _get_dialog_id(user_login);
    std::string login_sender = _get_login_sender(dialog_id);
    SACommand select_messages(m_con.get(),
        "select date, content from Message where dialog_id = :1");
    select_messages << (long)dialog_id;
    select_messages.Execute();

    std::vector<std::vector<std::string>> res;
    while(select_messages.FetchNext()) {
      res.push_back({login_sender, select_messages.Field("date").asString().GetMultiByteChars(),
          select_messages.Field("content").asString().GetMultiByteChars()});
    }
    return res;
  } 

  void onAccountLogin(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onAccountLogin, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    }
    spdlog::info("{} has logged in", login);
     
    asio::async_write(*m_sock.get(), asio::buffer("List dialogs [1] or Add contact [2]: \n"),
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onActionRequestSent(ec, bytes_transferred); 
        });
  }
  
  void onActionRequestSent(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onActionRequestSent, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    }
    spdlog::info("Action request sent");
    asio::async_read_until(*m_sock.get(), *(m_action_choice.get()), '\n',
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onActionResponseReceived(ec, bytes_transferred);
        });
  } 

  void onActionResponseReceived(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onActionResponseReceived, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    }

    std::istream istrm(m_action_choice.get());
    std::getline(istrm, action_choice);
    m_action_choice.reset(new asio::streambuf);

    spdlog::info("Action response received: {}", action_choice);
    if (action_choice == "1" || action_choice == "2") {
      onFinish();
      return;
      asio::async_write(*m_sock.get(), asio::buffer("Enter login: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onLoginRequestSent(ec, bytes_transferred); 
          });
    } else {
      asio::async_write(*m_sock.get(), asio::buffer("Input incorrect. Enter 1 or 2: \n"),
          [this](const system::error_code& ec, std::size_t bytes_transferred) {
            onActionRequestSent(ec, bytes_transferred); 
          });
    } 
  } 

  void onMessagesRequestReceived(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onMessagesRequestReceived, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    }
    std::string messages = "";

    std::vector<std::vector<std::string>> res = _get_messages(login);
    for (int i = 0; i < res.size(); i ++) {
      for (int j = 0; j < res[i].size(); j ++) {
        messages += res[i][j] + " ";
      } 
      messages += "\n";
    } 
    
    asio::async_write(*m_sock.get(), asio::buffer(messages),
        [this](const system::error_code& ec, std::size_t bytes_transferred) {
          onMessagesSent(ec, bytes_transferred); 
        });

  } 

  void onMessagesSent(const system::error_code& ec, std::size_t bytes_transferred) {
    if (ec.value() != 0) {
      spdlog::error("Error in onMessagesSent, code: {}, message: ", ec.value(), ec.message());
      onFinish();
      return;
    }
    onFinish();
  } 

  void onFinish() {
    delete this;
  } 

private:
  std::shared_ptr<asio::ip::tcp::socket> m_sock;
  std::string m_response, login, password="", sign_choice, action_choice;
  std::shared_ptr<asio::streambuf> m_request, m_login_buf, m_password_buf, m_sign_choice,
    m_action_choice;
  std::shared_ptr<SAConnection> m_con;
};


class Acceptor {
public:
  Acceptor(asio::io_context& ios, unsigned short port_num, std::shared_ptr<SAConnection> con):
    m_ios(ios),
    m_con(con),
    m_acceptor(m_ios, asio::ip::tcp::endpoint(asio::ip::address_v4::any(), port_num)),
    m_isStopped(false) {}

  void Start() {
    m_acceptor.listen();
    InitAccept();
  }

  void Stop() {
    m_isStopped.store(true);
  }

private:
  void InitAccept() {
    std::shared_ptr<asio::ip::tcp::socket> sock(new asio::ip::tcp::socket(m_ios));
    m_acceptor.async_accept(*sock.get(), [this, sock](const system::error_code& ec) {
          onAccept(ec, sock);
        });
  } 

  void onAccept(const system::error_code& ec, std::shared_ptr<asio::ip::tcp::socket> sock) {
    if (ec.value() == 0) {
      spdlog::info("Accepted connection from IP: {}", (*sock.get()).remote_endpoint().address().to_string());
      (new Service(sock, m_con))->StartHandling();
    } else {
      spdlog::error("Error in onAccept, code: {}, message: ", ec.value(), ec.message());
    }  

    if (!m_isStopped.load()) {
      InitAccept();
    } else {
      m_acceptor.close();
    } 
  } 

private:
  asio::io_context& m_ios;
  asio::ip::tcp::acceptor m_acceptor;
  std::atomic<bool> m_isStopped;
  std::shared_ptr<SAConnection> m_con;
}; 


class Server {
public:
  Server(std::string cfg_path) {
    std::ifstream cfg_istrm(cfg_path);
    json cfg = json::parse(cfg_istrm);

    std::string database_name = cfg["database_name"];
    std::string server_name = cfg["server_name"];
    std::string database_user = cfg["database_user"];
    std::string password = cfg["password"];
    
    std::string connection_string = server_name + "@" + database_name;

    std::cout << database_user << " " << password << std::endl;
    std::cout << connection_string << std::endl;
   
    con.reset(new SAConnection); 
    con->Connect(
        _TSA(connection_string.c_str()),
        _TSA(database_user.c_str()),
        _TSA(password.c_str()),
        SA_MySQL_Client);
    spdlog::info("Connected to database");
  }
  
  void Start(unsigned short port_num, unsigned int thread_pool_size) {
    assert(thread_pool_size > 0);
    acc.reset(new Acceptor(m_ios, port_num, con));
    acc->Start();

    for (int i = 0; i < thread_pool_size; i ++) {
      std::unique_ptr<std::thread> th(new std::thread(
            [this]() {
              m_ios.run();
            }));
      m_thread_pool.push_back(std::move(th));
    }   
  } 

  void Stop() {
    acc->Stop();
    m_ios.stop();

    for (auto& th: m_thread_pool) {
      th->join();
    } 
  } 

private:
  asio::io_context m_ios;
  std::unique_ptr<asio::io_context::work> m_work;
  std::unique_ptr<Acceptor> acc;
  std::vector<std::unique_ptr<std::thread>> m_thread_pool;
  std::shared_ptr<SAConnection> con;
};

const unsigned int DEFAULT_THREAD_POOL_SIZE = 2;

int main() {
  unsigned short port_num = 3333;

  spdlog::set_pattern("[%d/%m/%Y] [%H:%M:%S:%f] [%n] %^[%l]%$ %v"); 
  try {
    Server srv("../cfg_server.json");
    
    unsigned int thread_pool_size = std::thread::hardware_concurrency() * 2;
    if (thread_pool_size == 0) {
      thread_pool_size = DEFAULT_THREAD_POOL_SIZE;
    } 

    srv.Start(port_num, thread_pool_size);
    std::this_thread::sleep_for(std::chrono::seconds(50));
    srv.Stop();

  } 
  catch (system::system_error& e) {
    std::cout << e.code() << " " << e.what();
  } 
} 
