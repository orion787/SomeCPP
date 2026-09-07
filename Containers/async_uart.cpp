#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>

#include <iostream>
#include <array>
#include <string>
#include <thread>
#include <chrono>
#include <cerrno>
#include <cstring>

#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <sstream>
#include <atomic>
#include <condition_variable>

#ifdef __linux__
    #include <sys/stat.h>
    #include <unistd.h>
    #include <glob.h>
#endif


namespace asio = boost::asio;

using asio::serial_port;
using asio::serial_port_base;


// ============================================================
// КРОСС-ПЛАТФОРМЕННЫЕ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================


// ------------------------------------------------------------
// Проверка существования устройства
// ------------------------------------------------------------

bool device_exists(const std::string& path)
{
#ifdef __linux__

    struct stat st;

    if (stat(path.c_str(), &st) != 0)
    {
        return false;
    }

    return S_ISCHR(st.st_mode);

#else

    return true;

#endif
}


// ------------------------------------------------------------
// Список доступных serial-устройств
// ------------------------------------------------------------

std::vector<std::string> list_serial_devices()
{
    std::vector<std::string> result;

#ifdef __linux__

    auto scan = [&result](const char* pattern)
    {
        glob_t g;

        if (glob(pattern, GLOB_NOSORT, nullptr, &g) == 0)
        {
            for (size_t i = 0; i < g.gl_pathc; ++i)
            {
                std::string path = g.gl_pathv[i];

                if (device_exists(path))
                {
                    result.push_back(path);
                }
            }

            globfree(&g);
        }
    };

    scan("/dev/ttyUSB*");
    scan("/dev/ttyACM*");
    scan("/dev/ttyAMA*");
    scan("/dev/ttyS*");

#else

    for (int i = 1; i <= 10; ++i)
    {
        result.push_back(
            "COM" + std::to_string(i)
        );
    }

#endif

    return result;
}


// ------------------------------------------------------------
// Нормализация имени COM-порта Windows
// ------------------------------------------------------------

std::string normalize_device(const std::string& device)
{
#ifdef _WIN32

    if (device.size() >= 4 &&
        device.substr(0, 3) == "COM")
    {
        try
        {
            int number = std::stoi(
                device.substr(3)
            );

            if (number >= 10)
            {
                return "\\\\.\\" + device;
            }
        }
        catch (...)
        {
        }
    }

#endif

    return device;
}


// ------------------------------------------------------------
// Печать доступных устройств
// ------------------------------------------------------------

void print_available_devices()
{
    auto devices = list_serial_devices();

    if (devices.empty())
    {
        std::cout << "  (none found)\n";
        return;
    }

    for (const auto& device : devices)
    {
        std::cout
            << "  "
            << device
            << "\n";
    }
}


// ============================================================
// SERIAL COMMUNICATION
// ============================================================

class SerialCommunication
{
public:

    // ========================================================
    // CALLBACK TYPE
    // ========================================================

    using ReceiveCallback =
        std::function<void(const std::string&)>;


    // ========================================================
    // CONSTRUCTOR
    // ========================================================

    SerialCommunication(
        asio::io_context& io,
        const std::string& device,
        unsigned baud_rate
    )
        :
        io_(io),
        port_(io),
        device_(normalize_device(device)),
        baud_rate_(baud_rate)
    {
    }


    // ========================================================
    // DESTRUCTOR
    // ========================================================

    ~SerialCommunication()
    {
        stop();
    }


    // ========================================================
    // START
    // ========================================================

    void start()
    {
        if (running_)
        {
            return;
        }


#ifdef __linux__

        // Проверяем устройство до открытия
        if (!device_exists(device_))
        {
            std::ostringstream oss;

            oss
                << "Device '"
                << device_
                << "' does not exist.\n"
                << "Available devices:\n";

            auto devices =
                list_serial_devices();

            for (const auto& device : devices)
            {
                oss
                    << "  "
                    << device
                    << "\n";
            }

            if (devices.empty())
            {
                oss << "  (none found)\n";
            }

            throw std::runtime_error(
                oss.str()
            );
        }

#endif


        // ----------------------------------------------------
        // Открытие UART
        // ----------------------------------------------------

        boost::system::error_code ec;

        port_.open(
            device_,
            ec
        );

        if (ec)
        {
            std::ostringstream oss;

            oss
                << "Cannot open '"
                << device_
                << "': "
                << ec.message()
                << " (system error "
                << ec.value()
                << ")";

#ifdef __linux__

            if (ec.value() == 13)
            {
                oss
                    << "\nPermission denied.\n"
                    << "Fix:\n"
                    << "  sudo usermod -aG dialout $USER\n"
                    << "Then logout/login.";
            }

#endif

            throw std::runtime_error(
                oss.str()
            );
        }


        // ----------------------------------------------------
        // Настройки UART
        // ----------------------------------------------------

        port_.set_option(
            serial_port_base::baud_rate(
                baud_rate_
            ),
            ec
        );

        if (!ec)
        {
            port_.set_option(
                serial_port_base::character_size(8),
                ec
            );
        }

        if (!ec)
        {
            port_.set_option(
                serial_port_base::stop_bits(
                    serial_port_base::stop_bits::one
                ),
                ec
            );
        }

        if (!ec)
        {
            port_.set_option(
                serial_port_base::parity(
                    serial_port_base::parity::none
                ),
                ec
            );
        }

        if (!ec)
        {
            port_.set_option(
                serial_port_base::flow_control(
                    serial_port_base::flow_control::none
                ),
                ec
            );
        }


        if (ec)
        {
            port_.close();

            throw std::runtime_error(
                "Configure failed: " +
                ec.message()
            );
        }


        std::cout
            << "[serial] opened '"
            << device_
            << "' @ "
            << baud_rate_
            << " bps\n";


        running_ = true;


        // Запускаем постоянное чтение
        start_read();
    }


    // ========================================================
    // STOP
    // ========================================================

    void stop()
    {
        if (!running_)
        {
            return;
        }


        running_ = false;


        boost::system::error_code ec;


        port_.cancel(ec);

        port_.close(ec);


        // Разбудим возможный поток,
        // ожидающий wait_for_message()
        message_cv_.notify_all();


        std::cout
            << "[serial] stopped\n";
    }


    // ========================================================
    // SEND
    //
    // Потокобезопасный асинхронный метод.
    // Может вызываться из любого потока.
    // ========================================================

    void send(const std::string& message)
    {
        if (!running_)
        {
            std::cerr
                << "[serial] send ignored: UART not running\n";

            return;
        }


        std::string data =
            message;


        // Автоматически добавляем перевод строки
        if (data.empty() ||
            data.back() != '\n')
        {
            data += '\n';
        }


        asio::post(
            io_,

            [this,
             data = std::move(data)]() mutable
            {
                if (!running_)
                {
                    return;
                }


                bool write_in_progress =
                    !tx_queue_.empty();


                tx_queue_.push(
                    std::move(data)
                );


                if (!write_in_progress)
                {
                    start_write();
                }
            }
        );
    }


    // ========================================================
    // CALLBACK API
    //
    // Вызывается при получении полного сообщения.
    //
    // ВАЖНО:
    // Callback выполняется в потоке io_context.
    // ========================================================

    void set_receive_callback(
        ReceiveCallback callback
    )
    {
        std::lock_guard<std::mutex> lock(
            callback_mutex_
        );

        receive_callback_ =
            std::move(callback);
    }


    // ========================================================
    // QUEUE API
    //
    // Немедленная попытка получить сообщение.
    //
    // Возвращает:
    // true  - сообщение получено
    // false - очередь пуста
    // ========================================================

    bool try_get_message(
        std::string& message
    )
    {
        std::lock_guard<std::mutex> lock(
            message_mutex_
        );


        if (rx_queue_.empty())
        {
            return false;
        }


        message =
            std::move(
                rx_queue_.front()
            );


        rx_queue_.pop();


        return true;
    }


    // ========================================================
    // OPTIONAL BLOCKING API
    //
    // Ожидание сообщения с timeout.
    //
    // Основному приложению использовать необязательно.
    //
    // ========================================================

    bool wait_for_message(
        std::string& message,
        std::chrono::milliseconds timeout
    )
    {
        std::unique_lock<std::mutex> lock(
            message_mutex_
        );


        bool received =
            message_cv_.wait_for(
                lock,
                timeout,

                [this]()
                {
                    return !rx_queue_.empty()
                        || !running_;
                }
            );


        if (!received ||
            rx_queue_.empty())
        {
            return false;
        }


        message =
            std::move(
                rx_queue_.front()
            );


        rx_queue_.pop();


        return true;
    }


    // ========================================================
    // QUEUE SIZE
    // ========================================================

    std::size_t pending_messages() const
    {
        std::lock_guard<std::mutex> lock(
            message_mutex_
        );

        return rx_queue_.size();
    }


    // ========================================================
    // IS RUNNING
    // ========================================================

    bool is_running() const
    {
        return running_;
    }


private:

    // ========================================================
    // ASYNC READ LOOP
    // ========================================================

    void start_read()
    {
        if (!running_)
        {
            return;
        }


        port_.async_read_some(

            asio::buffer(
                read_buffer_
            ),

            [this](
                const boost::system::error_code& ec,
                std::size_t bytes_transferred
            )
            {
                if (ec == asio::error::operation_aborted)
                {
                    return;
                }


                if (ec)
                {
                    if (running_)
                    {
                        std::cerr
                            << "[serial rx] error: "
                            << ec.message()
                            << "\n";
                    }

                    return;
                }


                // Обрабатываем полученные байты
                handle_received_data(
                    read_buffer_.data(),
                    bytes_transferred
                );


                // Снова запускаем чтение
                start_read();
            }
        );
    }


    // ========================================================
    // HANDLE RECEIVED DATA
    //
    // UART является потоком байтов.
    // Сообщения разделяются символом '\n'.
    // ========================================================

    void handle_received_data(
        const char* data,
        std::size_t size
    )
    {
        rx_accumulator_.append(
            data,
            size
        );


        while (true)
        {
            std::size_t position =
                rx_accumulator_.find('\n');


            if (position ==
                std::string::npos)
            {
                break;
            }


            // Извлекаем одно сообщение
            std::string message =
                rx_accumulator_.substr(
                    0,
                    position
                );


            // Удаляем его из accumulator
            rx_accumulator_.erase(
                0,
                position + 1
            );


            // Поддержка CRLF
            if (!message.empty() &&
                message.back() == '\r')
            {
                message.pop_back();
            }


            // Игнорируем пустые строки
            if (message.empty())
            {
                continue;
            }


            // Передаём сообщение во все доступные механизмы
            dispatch_message(
                message
            );
        }
    }


    // ========================================================
    // DISPATCH MESSAGE
    //
    // Одно сообщение одновременно:
    //
    // 1. попадает в RX очередь
    // 2. передаётся callback
    //
    // ========================================================

    void dispatch_message(
        const std::string& message
    )
    {
        // ----------------------------------------------------
        // 1. Добавляем в очередь
        // ----------------------------------------------------

        {
            std::lock_guard<std::mutex> lock(
                message_mutex_
            );

            rx_queue_.push(
                message
            );
        }


        message_cv_.notify_one();


        // ----------------------------------------------------
        // 2. Вызываем callback
        // ----------------------------------------------------

        ReceiveCallback callback;


        {
            std::lock_guard<std::mutex> lock(
                callback_mutex_
            );

            callback =
                receive_callback_;
        }


        if (callback)
        {
            callback(
                message
            );
        }
    }


    // ========================================================
    // START WRITE
    // ========================================================

    void start_write()
    {
        if (tx_queue_.empty())
        {
            return;
        }


        asio::async_write(

            port_,

            asio::buffer(
                tx_queue_.front()
            ),

            [this](
                const boost::system::error_code& ec,
                std::size_t bytes_transferred
            )
            {
                if (ec)
                {
                    if (running_)
                    {
                        std::cerr
                            << "[serial tx] error: "
                            << ec.message()
                            << "\n";
                    }

                    return;
                }


                std::cout
                    << "[serial tx] "
                    << bytes_transferred
                    << " bytes\n";


                // Удаляем отправленное сообщение
                tx_queue_.pop();


                // Отправляем следующее
                if (!tx_queue_.empty())
                {
                    start_write();
                }
            }
        );
    }


private:

    // ========================================================
    // ASIO
    // ========================================================

    asio::io_context& io_;

    serial_port port_;


    // ========================================================
    // CONFIGURATION
    // ========================================================

    std::string device_;

    unsigned baud_rate_;


    // ========================================================
    // STATE
    // ========================================================

    std::atomic<bool> running_{false};


    // ========================================================
    // RX LOW LEVEL BUFFER
    // ========================================================

    std::array<char, 512> read_buffer_{};

    std::string rx_accumulator_;


    // ========================================================
    // RX MESSAGE QUEUE
    // ========================================================

    mutable std::mutex message_mutex_;

    std::condition_variable message_cv_;

    std::queue<std::string> rx_queue_;


    // ========================================================
    // TX QUEUE
    // ========================================================

    std::queue<std::string> tx_queue_;


    // ========================================================
    // CALLBACK
    // ========================================================

    mutable std::mutex callback_mutex_;

    ReceiveCallback receive_callback_;
};


// ============================================================
// MAIN
//
// СТРУКТУРА МАКСИМАЛЬНО БЛИЗКА К ИСХОДНОМУ ПРИМЕРУ
// ============================================================

int main(int argc, char** argv)
{
    try
    {
        // ====================================================
        // ARGUMENTS
        // ====================================================

        std::string device;

        unsigned baud = 115200;

        bool auto_detect = false;


        if (argc >= 2)
        {
            std::string arg1 =
                argv[1];


            // ------------------------------------------------
            // list
            // ------------------------------------------------

            if (arg1 == "list")
            {
                std::cout
                    << "Available serial devices:\n";

                print_available_devices();

                return 0;
            }


            // ------------------------------------------------
            // auto
            // ------------------------------------------------

            else if (arg1 == "auto")
            {
                auto_detect = true;
            }


            // ------------------------------------------------
            // explicit device
            // ------------------------------------------------

            else
            {
                device =
                    arg1;
            }
        }


        if (argc >= 3)
        {
            baud =
                static_cast<unsigned>(
                    std::stoul(argv[2])
                );
        }


        // ====================================================
        // DEFAULT DEVICE
        // ====================================================

#ifdef _WIN32

        std::string default_device =
            "COM3";

#else

        std::string default_device =
            "/dev/ttyUSB0";

#endif


        // ====================================================
        // AUTO DETECT
        // ====================================================

        if (auto_detect)
        {
            auto devices =
                list_serial_devices();


            if (devices.empty())
            {
                std::cerr
                    << "[error] No serial devices found.\n"
                    << "  - Connect USB-serial adapter\n"
                    << "  - Check: ls /dev/tty*\n"
                    << "  - Check: dmesg | tail -20\n";

                return 1;
            }


            device =
                devices.front();


            std::cout
                << "[auto] Selected: "
                << device
                << "\n";
        }


        // ====================================================
        // DEFAULT
        // ====================================================

        if (device.empty())
        {
            device =
                default_device;


            std::cout
                << "[info] Using default device: "
                << device
                << "\n"
                << "       Run with '"
                << argv[0]
                << " list' to see available devices\n";
        }


        // ====================================================
        // ASIO CONTEXT
        // ====================================================

        asio::io_context io;


        // ====================================================
        // UART OBJECT
        // ====================================================

        std::shared_ptr<SerialCommunication> uart;


        try
        {
            uart =
                std::make_shared<
                    SerialCommunication
                >(
                    io,
                    device,
                    baud
                );


            uart->start();
        }
        catch (const std::exception& e)
        {
            std::cerr
                << "\n[fatal] "
                << e.what()
                << "\n\n";


            std::cerr
                << "Tips:\n"
                << "  - Run: "
                << argv[0]
                << " list\n"
                << "  - Run: "
                << argv[0]
                << " auto\n";


#ifdef __linux__

            std::cerr
                << "  - On Linux: ls -la "
                << "/dev/ttyUSB* /dev/ttyACM*\n";

#endif

            return 1;
        }


        // ====================================================
        // CALLBACK MODE
        // ====================================================

        uart->set_receive_callback(

            [uart](
                const std::string& message
            )
            {
                std::cout
                    << "[callback rx] "
                    << message
                    << "\n";


                // Пример автоматического ответа

                if (message == "PING")
                {
                    uart->send(
                        "PONG"
                    );
                }
            }
        );


        // ====================================================
        // IO THREAD
        //
        // ВАЖНО:
        //
        // io.run() выполняется отдельно.
        //
        // Основной поток остаётся полностью свободным
        // для основной работы приложения.
        // ====================================================

        std::thread io_thread(

            [&io]()
            {
                try
                {
                    io.run();
                }
                catch (const std::exception& e)
                {
                    std::cerr
                        << "[io] exception: "
                        << e.what()
                        << "\n";
                }
            }
        );


        // ====================================================
        // MAIN APPLICATION LOOP
        // ====================================================

        int tick = 0;


        while (true)
        {
            // =================================================
            // ОСНОВНАЯ ЛОГИКА ПРИЛОЖЕНИЯ
            // =================================================

            std::cout
                << "[main] application working, tick="
                << tick
                << "\n";


            // =================================================
            // QUEUE / POLLING MODE
            //
            // Забираем все накопившиеся команды.
            //
            // try_get_message() НЕ блокирует поток.
            // =================================================

            std::string message;


            while (
                uart->try_get_message(
                    message
                )
            )
            {
                std::cout
                    << "[main queue rx] "
                    << message
                    << "\n";


                // ---------------------------------------------
                // Здесь можно выполнять реальную
                // переконфигурацию приложения
                // ---------------------------------------------

                if (message == "CONFIG:MODE=A")
                {
                    std::cout
                        << "[main] Switching to MODE A\n";
                }


                else if (message == "CONFIG:MODE=B")
                {
                    std::cout
                        << "[main] Switching to MODE B\n";
                }


                else if (message == "STATUS?")
                {
                    uart->send(
                        "STATUS:WORKING"
                    );
                }
            }


            // =================================================
            // ПЕРИОДИЧЕСКАЯ ОТПРАВКА СТАТУСА
            // =================================================

            if (tick % 5 == 0)
            {
                uart->send(
                    "STATUS:WORKING"
                );
            }


            ++tick;


            // Имитация работы приложения
            std::this_thread::sleep_for(
                std::chrono::seconds(1)
            );
        }


        // ====================================================
        // SHUTDOWN
        // ====================================================

        uart->stop();

        io.stop();


        if (io_thread.joinable())
        {
            io_thread.join();
        }


        return 0;
    }


    catch (const std::exception& e)
    {
        std::cerr
            << "[fatal] "
            << e.what()
            << "\n";

        return 1;
    }
}


/*SerialCommunication uart(io, device, 115200);

uart.start();

uart.set_receive_callback(
    [&uart](const std::string& message)
    {
        if (message == "PING")
        {
            uart.send("PONG");
        }
    }
);

std::thread io_thread(
    [&io]()
    {
        io.run();
    }
);


while (application_running)
{
    //----------------------------------------------------------
    // Основная работа старого приложения
    //----------------------------------------------------------

    RunApplicationLogic();


    //----------------------------------------------------------
    // Проверяем новые команды UART
    //----------------------------------------------------------

    std::string command;

    while (uart.try_get_message(command))
    {
        ProcessConfigurationCommand(command);
    }


    //----------------------------------------------------------
    // При необходимости отправляем состояние
    //----------------------------------------------------------

    if (StageChanged())
    {
        uart.send(
            GetCurrentStageText()
        );
    }
}*/