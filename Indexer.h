#pragma once

#include <string>
#include <vector>
#include <map>
#include <utility>
#include <pqxx/pqxx>

class Indexer
{
public:
    Indexer();

    // Автоматическое создание таблиц PostgreSQL
    void createTables(pqxx::connection& connection);

    // Обход папки и сохранение результатов в PostgreSQL
    void indexDirectory(
        const std::string& directory,
        const std::vector<std::string>& extensions,
        pqxx::connection& connection
    );

    // Чтение текстового файла
    std::string readFile(const std::string& filePath);

    // Разделение текста на слова
    std::vector<std::string> getWords(const std::string& text);

    // Поиск документов по словам
    std::vector<std::pair<std::string, int>> searchDocuments(
        const std::vector<std::string>& words,
        pqxx::connection& connection,
        int maxResults
    );

private:
    // Подсчёт частоты слов
    std::map<std::string, int> countWords(const std::string& text);
};