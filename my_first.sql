SELECT
    (SELECT COUNT(*) FROM documents) AS documents_count,
    (SELECT COUNT(*) FROM words) AS words_count,
    (SELECT COUNT(*) FROM document_words) AS relations_count;