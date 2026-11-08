
#include "Indexer.h"
#include "ThreadPool.h"

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <windows.h>
#include <clocale>
#include <functional>
#include <future>

// Удаление пробелов в начале и конце строки
std::string trim(const std::string& value)
{
    const std::string whitespace = " \t\r\n";

    size_t first = value.find_first_not_of(whitespace);

    if (first == std::string::npos)
    {
        return "";
    }

    size_t last = value.find_last_not_of(whitespace);

    return value.substr(first, last - first + 1);
}

// Чтение INI-файла
std::map<std::string, std::string> readConfig(
    const std::string& fileName)
{
    std::ifstream file(fileName);

    if (!file.is_open())
    {
        throw std::runtime_error(
            "Cannot open configuration file: " + fileName
        );
    }

    std::map<std::string, std::string> settings;

    std::string line;

    while (std::getline(file, line))
    {
        line = trim(line);

        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }

        // Пропускаем названия секций
        if (line.front() == '[' && line.back() == ']')
        {
            continue;
        }

        size_t separator = line.find('=');

        if (separator == std::string::npos)
        {
            continue;
        }

        std::string key = trim(line.substr(0, separator));
        std::string value = trim(line.substr(separator + 1));

        if (!key.empty())
        {
            settings[key] = value;
        }
    }

    return settings;
}

// Получение настройки из конфигурации
std::string getSetting(
    const std::map<std::string, std::string>& settings,
    const std::string& key)
{
    auto it = settings.find(key);

    if (it == settings.end() || it->second.empty())
    {
        throw std::runtime_error(
            "Missing configuration parameter: " + key
        );
    }

    return it->second;
}

// Разделение расширений файлов через запятую
std::vector<std::string> parseExtensions(
    const std::string& value)
{
    std::vector<std::string> extensions;

    std::stringstream stream(value);

    std::string extension;

    while (std::getline(stream, extension, ','))
    {
        extension = trim(extension);

        if (!extension.empty())
        {
            if (extension[0] != '.')
            {
                extension.insert(0, ".");
            }

            std::transform(
                extension.begin(),
                extension.end(),
                extension.begin(),
                [](unsigned char symbol)
                {
                    return static_cast<char>(
                        std::tolower(symbol)
                        );
                }
            );

            extensions.push_back(extension);
        }
    }

    if (extensions.empty())
    {
        throw std::runtime_error(
            "No file extensions specified in Config.ini."
        );
    }

    return extensions;
}

// Чтение строки из консоли с поддержкой русского языка
std::string readConsoleLine()
{
    HANDLE console = GetStdHandle(STD_INPUT_HANDLE);

    if (console == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error(
            "Cannot access console input."
        );
    }

    std::wstring wideInput;
    wchar_t symbol;

    while (true)
    {
        DWORD readCount = 0;

        if (!ReadConsoleW(
            console,
            &symbol,
            1,
            &readCount,
            nullptr))
        {
            throw std::runtime_error(
                "Cannot read console input."
            );
        }

        if (readCount == 0)
        {
            break;
        }

        if (symbol == L'\r')
        {
            continue;
        }

        if (symbol == L'\n')
        {
            break;
        }

        wideInput += symbol;
    }

    if (wideInput.empty())
    {
        return "";
    }

    int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wideInput.c_str(),
        static_cast<int>(wideInput.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (size == 0)
    {
        throw std::runtime_error(
            "Cannot convert console input to UTF-8."
        );
    }

    std::string result(size, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        wideInput.c_str(),
        static_cast<int>(wideInput.size()),
        result.data(),
        size,
        nullptr,
        nullptr
    );

    return result;
}

// Экранирование значений для строки подключения PostgreSQL
std::string quoteConnectionValue(const std::string& value)
{
    std::string result = "'";

    for (char symbol : value)
    {
        if (symbol == '\\' || symbol == '\'')
        {
            result += '\\';
        }

        result += symbol;
    }

    result += "'";

    return result;
}

// Формирование строки подключения
std::string createConnectionString(
    const std::string& host,
    const std::string& port,
    const std::string& database,
    const std::string& user,
    const std::string& password)
{
    return
        "host=" + quoteConnectionValue(host) +
        " port=" + quoteConnectionValue(port) +
        " dbname=" + quoteConnectionValue(database) +
        " user=" + quoteConnectionValue(user) +
        " password=" + quoteConnectionValue(password);
}

int main()
{
    // Настройка кодировки консоли
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    setlocale(LC_ALL, ".UTF-8");

    try
    {
        // Проверка ThreadPool
        ThreadPool pool(3);

        auto task1 = pool.enqueue([]()
            {
                return 10 + 20;
            });

        auto task2 = pool.enqueue([]()
            {
                return 30 + 40;
            });

        auto task3 = pool.enqueue([]()
            {
                return 50 + 60;
            });

        std::cout << "ThreadPool test:" << std::endl;
        std::cout << task1.get() << std::endl;
        std::cout << task2.get() << std::endl;
        std::cout << task3.get() << std::endl;

        // Чтение конфигурации
        std::cout << "Reading config.ini..." << std::endl;

        std::map<std::string, std::string> settings =
            readConfig("Config.ini");

        std::cout << "Config loaded successfully!"
            << std::endl;

        // Получение параметров PostgreSQL
        std::string host = getSetting(settings, "host");
        std::string port = getSetting(settings, "port");
        std::string database = getSetting(settings, "dbname");
        std::string user = getSetting(settings, "user");

        // Получение параметров индексатора
        std::string directory =
            getSetting(settings, "directory");

        std::vector<std::string> extensions =
            parseExtensions(
                getSetting(settings, "extensions")
            );

        // Получение параметров поиска
        int maxResults = std::stoi(
            getSetting(settings, "max_results")
        );

        int maxQueryWords = std::stoi(
            getSetting(settings, "max_query_words")
        );

        // Проверка корректности настроек
        if (maxResults <= 0)
        {
            throw std::runtime_error(
                "max_results must be greater than zero."
            );
        }

        if (maxQueryWords <= 0)
        {
            throw std::runtime_error(
                "max_query_words must be greater than zero."
            );
        }

        // По ТЗ максимум 10 результатов
        maxResults = (std::min)(maxResults, 10);

        // По ТЗ максимум 4 слова в запросе
        maxQueryWords = (std::min)(maxQueryWords, 4);

        // Вывод параметров
        std::cout << "Database: "
            << database << std::endl;

        std::cout << "Directory: "
            << directory << std::endl;

        std::cout << "Extensions: ";

        for (size_t i = 0; i < extensions.size(); ++i)
        {
            if (i > 0)
            {
                std::cout << ",";
            }

            std::cout << extensions[i];
        }

        std::cout << std::endl;

        std::cout << "Maximum results: "
            << maxResults << std::endl;

        std::cout << "Maximum query words: "
            << maxQueryWords << std::endl;

        // Запрос пароля PostgreSQL
        std::cout << "\nEnter PostgreSQL password: ";

        std::string password;

        std::getline(std::cin, password);

        if (password.empty())
        {
            throw std::runtime_error(
                "PostgreSQL password cannot be empty."
            );
        }

        // Создание строки подключения
        std::string connectionString =
            createConnectionString(
                host,
                port,
                database,
                user,
                password
            );

        // Подключение к PostgreSQL
        pqxx::connection connection(connectionString);

        std::cout << "\nConnected to PostgreSQL!"
            << std::endl;

        // Проверка подключения
        pqxx::work checkTransaction(connection);

        pqxx::result databaseInfo = checkTransaction.exec(
            "SELECT current_database(), current_user;"
        );

        std::cout << "Database: "
            << databaseInfo[0][0].as<std::string>()
            << std::endl;

        std::cout << "User: "
            << databaseInfo[0][1].as<std::string>()
            << std::endl;

        checkTransaction.commit();

        // Создание объекта индексатора
        Indexer indexer;

        // Создание таблиц при первом запуске
        std::cout << "\nChecking database tables..."
            << std::endl;

        indexer.createTables(connection);

        // Индексация файлов
        std::cout << "\nStarting directory indexing..."
            << std::endl;

        indexer.indexDirectory(
            directory,
            extensions,
            connection
        );

        std::cout << "\nDirectory indexing completed!"
            << std::endl;

        // Поиск документов
        std::cout << "\n========== DOCUMENT SEARCH =========="
            << std::endl;

        std::cout << "Enter search query (maximum "
            << maxQueryWords << " words): ";

        std::string searchQuery = readConsoleLine();

        // Обработка поискового запроса
        std::vector<std::string> searchWords =
            indexer.getWords(searchQuery);

        if (searchWords.empty())
        {
            std::cout << "\nSearch query contains no valid words."
                << std::endl;

            return 0;
        }

        // Проверка количества слов
        if (searchWords.size() > static_cast<size_t>(maxQueryWords))
        {
            std::cout << "\nToo many words in search query."
                << std::endl;

            std::cout << "Maximum allowed: "
                << maxQueryWords << std::endl;

            return 0;
        }

        std::cout << "\nSearching documents..."
            << std::endl;

        // Выполнение поиска
        std::vector<std::pair<std::string, int>> results =
            indexer.searchDocuments(
                searchWords,
                connection,
                maxResults
            );

        std::cout << "\n========== SEARCH RESULTS =========="
            << std::endl;

        if (results.empty())
        {
            std::cout << "No documents found."
                << std::endl;
        }
        else
        {
            std::cout << "Found documents: "
                << results.size() << std::endl;

            std::cout << "------------------------------------"
                << std::endl;

            for (size_t i = 0; i < results.size(); ++i)
            {
                std::cout << i + 1 << ". "
                    << results[i].first << std::endl;

                std::cout << "   Relevance: "
                    << results[i].second << std::endl;

                std::cout << "------------------------------------"
                    << std::endl;
            }
        }

        std::cout << "\nSearch completed!"
            << std::endl;

        return 0;
    }
    catch (const pqxx::sql_error& error)
    {
        std::cerr << "\nDatabase error: "
            << error.what() << std::endl;

        return 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "\nError: "
            << error.what() << std::endl;

        return 1;
    }
    catch (...)
    {
        std::cerr << "\nInternal error."
            << std::endl;

        return 1;
    }
}
