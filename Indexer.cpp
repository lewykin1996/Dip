
#include "Indexer.h"
#include "ThreadPool.h"

#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <future>
#include <windows.h>
#include <cctype>
#include <unordered_set>

// Преобразование UTF-8 в wide string
std::wstring utf8ToWide(const std::string& text)
{
    if (text.empty())
        return L"";

    int size = MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), -1, nullptr, 0
    );

    if (size == 0)
        return L"";

    std::wstring result(size, L'\0');

    MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), -1,
        result.data(), size
    );

    result.pop_back();

    return result;
}

// Преобразование wide string в UTF-8
std::string wideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return "";

    int size = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), -1,
        nullptr, 0, nullptr, nullptr
    );

    if (size == 0)
        return "";

    std::string result(size, '\0');

    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), -1,
        result.data(), size, nullptr, nullptr
    );

    result.pop_back();

    return result;
}

// Перевод текста в нижний регистр
std::wstring toLowerCase(const std::wstring& text)
{
    if (text.empty())
        return L"";

    int size = LCMapStringEx(
        LOCALE_NAME_USER_DEFAULT,
        LCMAP_LOWERCASE,
        text.c_str(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr,
        0
    );

    if (size == 0)
        return text;

    std::wstring result(size, L'\0');

    LCMapStringEx(
        LOCALE_NAME_USER_DEFAULT,
        LCMAP_LOWERCASE,
        text.c_str(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr,
        0
    );

    return result;
}

// Проверка: буква или цифра
bool isLetterOrDigit(wchar_t symbol)
{
    WORD type = 0;

    if (!GetStringTypeW(CT_CTYPE1, &symbol, 1, &type))
        return false;

    return (type & (C1_ALPHA | C1_DIGIT)) != 0;
}

// Подсчёт количества символов Unicode
int countCharacters(const std::wstring& text)
{
    int count = 0;

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] >= 0xD800 && text[i] <= 0xDBFF)
        {
            if (i + 1 < text.size() &&
                text[i + 1] >= 0xDC00 &&
                text[i + 1] <= 0xDFFF)
            {
                ++i;
            }
        }

        ++count;
    }

    return count;
}

// Нормализация расширения файла
std::string normalizeExtension(std::string extension)
{
    size_t first = extension.find_first_not_of(" \t\r\n");

    if (first == std::string::npos)
        return "";

    size_t last = extension.find_last_not_of(" \t\r\n");

    extension = extension.substr(first, last - first + 1);

    if (!extension.empty() && extension[0] != '.')
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

    return extension;
}

Indexer::Indexer()
{
}

// Автоматическое создание таблиц PostgreSQL
void Indexer::createTables(pqxx::connection& connection)
{
    pqxx::work transaction(connection);

    transaction.exec(
        "CREATE TABLE IF NOT EXISTS documents ("
        "id SERIAL PRIMARY KEY, "
        "file_path TEXT NOT NULL UNIQUE"
        ");"
    );

    transaction.exec(
        "CREATE TABLE IF NOT EXISTS words ("
        "id SERIAL PRIMARY KEY, "
        "word VARCHAR(32) NOT NULL UNIQUE"
        ");"
    );

    transaction.exec(
        "CREATE TABLE IF NOT EXISTS document_words ("
        "document_id INTEGER NOT NULL, "
        "word_id INTEGER NOT NULL, "
        "frequency INTEGER NOT NULL CHECK (frequency > 0), "
        "PRIMARY KEY (document_id, word_id), "
        "FOREIGN KEY (document_id) REFERENCES documents(id) ON DELETE CASCADE, "
        "FOREIGN KEY (word_id) REFERENCES words(id) ON DELETE CASCADE"
        ");"
    );

    transaction.commit();

    std::cout << "Database tables are ready!" << std::endl;
}

// Разделение текста на слова
std::vector<std::string> Indexer::getWords(
    const std::string& text)
{
    std::vector<std::string> words;

    std::wstring wideText = utf8ToWide(text);
    wideText = toLowerCase(wideText);

    std::wstring currentWord;

    for (wchar_t symbol : wideText)
    {
        if (isLetterOrDigit(symbol))
        {
            currentWord += symbol;
        }
        else
        {
            if (!currentWord.empty())
            {
                int length = countCharacters(currentWord);

                if (length >= 3 && length <= 32)
                {
                    words.push_back(wideToUtf8(currentWord));
                }

                currentWord.clear();
            }
        }
    }

    if (!currentWord.empty())
    {
        int length = countCharacters(currentWord);

        if (length >= 3 && length <= 32)
        {
            words.push_back(wideToUtf8(currentWord));
        }
    }

    return words;
}

// Подсчёт частоты слов
std::map<std::string, int> Indexer::countWords(
    const std::string& text)
{
    std::map<std::string, int> wordCounts;

    std::vector<std::string> words = getWords(text);

    for (const std::string& word : words)
    {
        ++wordCounts[word];
    }

    return wordCounts;
}

// Чтение текстового файла
std::string Indexer::readFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary);

    if (!file.is_open())
    {
        std::cerr << "Cannot open file: "
            << filePath << std::endl;

        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}

// Обход папки и многопоточная индексация файлов
void Indexer::indexDirectory(
    const std::string& directory,
    const std::vector<std::string>& extensions,
    pqxx::connection& connection)
{
    namespace fs = std::filesystem;

    using WordCounts = std::map<std::string, int>;
    using FileResult = std::pair<std::string, WordCounts>;

    // Подготавливаем список расширений
    std::vector<std::string> allowedExtensions;

    for (const std::string& extension : extensions)
    {
        std::string normalized = normalizeExtension(extension);

        if (!normalized.empty())
        {
            allowedExtensions.push_back(normalized);
        }
    }

    if (allowedExtensions.empty())
    {
        std::cout << "No file extensions specified!"
            << std::endl;

        return;
    }

    // Проверяем существование каталога
    if (!fs::exists(directory) || !fs::is_directory(directory))
    {
        throw std::runtime_error(
            "Directory does not exist or is not a directory: "
            + directory
        );
    }

    // Определяем количество потоков
    size_t threadCount = std::thread::hardware_concurrency();

    if (threadCount == 0)
    {
        threadCount = 2;
    }

    std::cout << "ThreadPool started. Threads: "
        << threadCount << std::endl;

    ThreadPool pool(threadCount);

    // Собираем пути к файлам с нужными расширениями
    std::vector<std::string> filePaths;

    for (const auto& entry :
        fs::recursive_directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        std::string fileExtension =
            normalizeExtension(entry.path().extension().string());

        bool extensionAllowed =
            std::find(
                allowedExtensions.begin(),
                allowedExtensions.end(),
                fileExtension
            ) != allowedExtensions.end();

        if (extensionAllowed)
        {
            filePaths.push_back(
                fs::absolute(entry.path()).lexically_normal().string()
            );
        }
    }

    // Удаляем устаревшие документы из базы данных
    std::unordered_set<std::string> currentFiles;

    for (const std::string& filePath : filePaths)
    {
        currentFiles.insert(
            fs::path(filePath).lexically_normal().string()
        );
    }

    fs::path rootPath =
        fs::absolute(directory).lexically_normal();

    pqxx::work cleanupTransaction(connection);

    pqxx::result existingDocuments = cleanupTransaction.exec(
        "SELECT id, file_path FROM documents;"
    );

    int deletedDocuments = 0;

    for (const auto& row : existingDocuments)
    {
        int documentId = row[0].as<int>();
        std::string storedPath = row[1].as<std::string>();

        fs::path documentPath =
            fs::path(storedPath).lexically_normal();

        fs::path relativePath =
            documentPath.lexically_relative(rootPath);

        // Не трогаем документы, находящиеся вне индексируемого каталога
        if (relativePath.empty() ||
            relativePath.is_absolute() ||
            *relativePath.begin() == "..")
        {
            continue;
        }

        std::string normalizedPath = documentPath.string();

        // Удаляем документ, если его больше нет среди найденных файлов
        if (currentFiles.find(normalizedPath) == currentFiles.end())
        {
            cleanupTransaction.exec(
                "DELETE FROM documents WHERE id = $1;",
                pqxx::params{ documentId }
            );

            ++deletedDocuments;
        }
    }

    cleanupTransaction.commit();

    std::cout << "Deleted outdated documents: "
        << deletedDocuments << std::endl;

    std::cout << "Found files: "
        << filePaths.size() << std::endl;

    // Отправляем обработку файлов в пул потоков
    std::vector<std::future<FileResult>> futures;

    for (const std::string& filePath : filePaths)
    {
        futures.push_back(pool.enqueue([this, filePath]()
            {
                // Чтение файла
                std::string text = readFile(filePath);

                if (text.empty())
                {
                    return FileResult{
                        filePath,
                        WordCounts{}
                    };
                }

                // Подсчёт частоты слов
                WordCounts wordCounts = countWords(text);

                return FileResult{
                    filePath,
                    wordCounts
                };
            }));
    }

    // Получаем результаты и сохраняем их в PostgreSQL
    for (auto& future : futures)
    {
        FileResult result = future.get();

        const std::string& filePath = result.first;
        const WordCounts& wordCounts = result.second;

        if (wordCounts.empty())
        {
            std::cout << "Empty or unreadable file: "
                << filePath << std::endl;

            continue;
        }

        pqxx::work transaction(connection);

        // Добавление документа или получение его ID
        pqxx::result documentResult = transaction.exec(
            "INSERT INTO documents (file_path) "
            "VALUES ($1) "
            "ON CONFLICT (file_path) "
            "DO UPDATE SET file_path = EXCLUDED.file_path "
            "RETURNING id;",
            pqxx::params{ filePath }
        );

        int documentId = documentResult[0][0].as<int>();

        // Удаление старых связей документа со словами
        transaction.exec(
            "DELETE FROM document_words WHERE document_id = $1;",
            pqxx::params{ documentId }
        );

        // Сохранение слов и их частот
        for (const auto& pair : wordCounts)
        {
            const std::string& word = pair.first;
            int frequency = pair.second;

            pqxx::result wordResult = transaction.exec(
                "INSERT INTO words (word) "
                "VALUES ($1) "
                "ON CONFLICT (word) "
                "DO UPDATE SET word = EXCLUDED.word "
                "RETURNING id;",
                pqxx::params{ word }
            );

            int wordId = wordResult[0][0].as<int>();

            transaction.exec(
                "INSERT INTO document_words "
                "(document_id, word_id, frequency) "
                "VALUES ($1, $2, $3);",
                pqxx::params{
                    documentId,
                    wordId,
                    frequency
                }
            );
        }

        // Подтверждение транзакции
        transaction.commit();

        std::cout << "File saved: "
            << filePath << std::endl;

        std::cout << "Unique words: "
            << wordCounts.size() << std::endl;

        std::cout << "------------------------"
            << std::endl;
    }

    std::cout << "Directory indexing completed!"
        << std::endl;
}

// Поиск документов по словам
std::vector<std::pair<std::string, int>>
Indexer::searchDocuments(
    const std::vector<std::string>& words,
    pqxx::connection& connection,
    int maxResults)
{
    std::vector<std::pair<std::string, int>> results;

    if (words.empty() || maxResults <= 0)
    {
        return results;
    }

    // Удаляем повторяющиеся слова из запроса
    std::vector<std::string> uniqueWords;

    for (const auto& word : words)
    {
        if (std::find(
            uniqueWords.begin(),
            uniqueWords.end(),
            word
        ) == uniqueWords.end())
        {
            uniqueWords.push_back(word);
        }
    }

    // Формируем SQL-запрос
    std::string query =
        "SELECT d.file_path, SUM(dw.frequency) AS relevance "
        "FROM documents d "
        "JOIN document_words dw ON dw.document_id = d.id "
        "JOIN words w ON w.id = dw.word_id "
        "WHERE w.word IN (";

    for (size_t i = 0; i < uniqueWords.size(); ++i)
    {
        if (i > 0)
        {
            query += ", ";
        }

        query += "$" + std::to_string(i + 1);
    }

    query += ") "
        "GROUP BY d.id, d.file_path "
        "HAVING COUNT(DISTINCT w.word) = $" +
        std::to_string(uniqueWords.size() + 1);

    query += " ORDER BY relevance DESC, d.file_path LIMIT $" +
        std::to_string(uniqueWords.size() + 2);

    // Передаём параметры в PostgreSQL
    pqxx::params params;

    for (const auto& word : uniqueWords)
    {
        params.append(word);
    }

    params.append(static_cast<int>(uniqueWords.size()));
    params.append(maxResults);

    // Выполняем запрос
    pqxx::work transaction(connection);

    pqxx::result dbResults = transaction.exec(query, params);

    for (const auto& row : dbResults)
    {
        std::string filePath = row[0].as<std::string>();
        int relevance = row[1].as<int>();

        results.emplace_back(filePath, relevance);
    }

    transaction.commit();

    return results;
}
