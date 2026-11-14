
#include "Indexer.h"

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
#include <cctype>

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

// Чтение Config.ini
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

// Получение параметра конфигурации
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

// Разделение расширений файлов
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
                    return static_cast<char>(std::tolower(symbol));
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

// Экранирование значений для PostgreSQL
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
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    setlocale(LC_ALL, ".UTF-8");

    try
    {
        // Чтение конфигурации
        std::cout << "Reading Config.ini..." << std::endl;

        std::map<std::string, std::string> settings =
            readConfig("Config.ini");

        std::cout << "Config loaded successfully!" << std::endl;

        // Получение параметров подключения
        std::string host = getSetting(settings, "host");
        std::string port = getSetting(settings, "port");
        std::string database = getSetting(settings, "dbname");
        std::string user = getSetting(settings, "user");

        // Получение параметров индексации
        std::string directory =
            getSetting(settings, "directory");

        std::vector<std::string> extensions =
            parseExtensions(
                getSetting(settings, "extensions")
            );

        std::cout << "Database: " << database << std::endl;
        std::cout << "Directory: " << directory << std::endl;

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

        // Подключение к PostgreSQL
        std::string connectionString =
            createConnectionString(
                host,
                port,
                database,
                user,
                password
            );

        pqxx::connection connection(connectionString);

        std::cout << "\nConnected to PostgreSQL!" << std::endl;

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

        // Создание таблиц
        std::cout << "\nChecking database tables..."
            << std::endl;

        indexer.createTables(connection);

        // Индексация документов
        std::cout << "\nStarting directory indexing..."
            << std::endl;

        indexer.indexDirectory(
            directory,
            extensions,
            connection
        );

        std::cout << "\nIndexer application completed!"
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
        std::cerr << "\nInternal error." << std::endl;

        return 1;
    }
}
