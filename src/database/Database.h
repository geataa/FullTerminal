#pragma once
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ft::db {
enum class Engine { SQLite, PostgreSQL, MySQL, SqlServer, Oracle, Odbc };
enum class State { Disconnected, Connecting, Connected, Lost };
struct Profile {
    std::wstring id, name, host = L"localhost", database, user, password, driver, dsn, caFile;
    Engine engine = Engine::PostgreSQL;
    int port = 5432;
    bool integrated = false, readOnly = false, encrypt = true, trustCertificate = false;
    std::string protectedPassword;
    bool secretLocked = false;
};
std::wstring EngineName(Engine engine);
std::vector<std::wstring> InstalledDrivers();
std::wstring DefaultDriver(Engine engine, const std::vector<std::wstring>& drivers);
std::wstring ConnectionString(const Profile& profile);
bool LoadProfiles(const std::wstring& dir, std::vector<Profile>& profiles, std::wstring& error);
bool SaveProfiles(const std::wstring& dir, const std::vector<Profile>& profiles, std::wstring& error);

struct Value {
    std::optional<std::wstring> text;
    bool binary = false, truncated = false;
    bool operator==(const Value&) const = default;
};
struct Column {
    std::wstring name, type;
    int sqlType = 0;
    int64_t size = 0;
    bool nullable = true, primaryKey = false, generated = false;
};
using Row = std::vector<Value>;
struct Table {
    std::wstring catalog, schema, name, type;
    bool operator==(const Table&) const = default;
};
struct ResultSet {
    std::vector<Column> columns;
    std::vector<Row> rows;
    int64_t affected = -1;
    bool limited = false;
};
struct Result {
    std::vector<ResultSet> sets;
    std::vector<Table> tables;
    std::vector<std::wstring> databases;
    std::wstring error, sqlState, information;
    bool lost = false, cancelled = false, transaction = false;
    double milliseconds = 0;
};
struct Parameter { Value value; int sqlType = 0; int64_t size = 0; };
struct Statement { std::wstring sql; std::vector<Parameter> parameters; bool expectOne = false; };
enum class Action { Connect, Disconnect, Query, Catalog, Describe, Edit, AutoCommit, Commit, Rollback, Health };
struct Request {
    Action action = Action::Query;
    uint64_t document = 0;
    std::wstring sql;
    Table table;
    std::vector<Parameter> parameters;
    std::vector<Statement> edits;
    bool flag = true;
    int rowLimit = 500;
};

// Connection ownership stays on one worker. Cancel is the only concurrent API.
class Backend {
public:
    virtual ~Backend() = default;
    virtual Result Open(const Profile&) = 0;
    virtual Result Run(const Request&) = 0;
    virtual void Cancel() = 0;
};
std::unique_ptr<Backend> MakeBackend(Engine engine);
std::unique_ptr<Backend> MakeSqliteBackend();
std::unique_ptr<Backend> MakeOdbcBackend();

std::wstring QuoteIdentifier(Engine engine, const std::wstring& identifier);
std::wstring QualifiedName(Engine engine, const Table& table);
std::wstring SelectTable(Engine engine, const Table& table, const std::vector<Column>& columns,
                         int sortColumn, bool descending, int offset, int limit,
                         int filterColumn = -1, bool filterNull = false);
std::optional<Statement> UpdateRow(Engine engine, const Table& table, const std::vector<Column>& columns,
                                  const Row& original, const Row& updated, bool remove, std::wstring& error);
std::optional<Statement> InsertRow(Engine engine, const Table& table, const std::vector<Column>& columns,
                                  const Row& row, const std::vector<bool>& include, std::wstring& error);
std::wstring Csv(const ResultSet& result);
std::wstring JsonRows(const ResultSet& result);

struct Document {
    uint64_t id = 0;
    int editorId = 0;
    std::wstring name, sql = L"SELECT 1;", savedSql, filter;
    bool tableView = false, chart = false, structure = false, filterNull = false, descending = false;
    Table table;
    std::vector<Column> tableColumns;
    Result result;
    size_t resultIndex = 0;
    int sortColumn = -1, filterColumn = -1, offset = 0, pageSize = 200, scrollRow = 0, scrollColumn = 0;
    int selectedRow = -1, selectedColumn = 0, chartColumn = 1;
    Row editRow;
    std::vector<bool> include;
    bool editing = false, inserting = false;
    int editScroll = 0;
};

class Session {
public:
    explicit Session(Profile profile, std::unique_ptr<Backend> backend = {});
    ~Session();
    Session(const Session&) = delete;
    bool Submit(Request request);
    void Cancel();
    bool Poll();
    void Connect();
    void RefreshCatalog();
    uint64_t NewEditor(const std::wstring& sql = L"SELECT 1;");
    uint64_t OpenTable(const Table& table);
    bool Execute(uint64_t document, const std::wstring& sql = L"");
    bool LoadTable(uint64_t document);
    bool SaveRow(uint64_t document, bool remove, std::wstring& error);
    Document* Find(uint64_t id);
    Document* Active();
    bool HasUnsaved() const;
    const Profile& Connection() const { return m_profile; }
    State state = State::Disconnected;
    bool busy = false, autoCommit = true, transaction = false, closeRequested = false;
    std::wstring error, notice;
    std::vector<Table> tables;
    std::vector<std::wstring> databases;
    std::vector<Document> documents;
    std::vector<std::wstring> history;
    uint64_t active = 0;
    int explorerScroll = 0, subScroll = 0;
    std::wstring search;
private:
    void Work();
    Profile m_profile;
    std::unique_ptr<Backend> m_backend;
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::optional<Request> m_request;
    struct Completion { Request request; Result result; };
    std::optional<Completion> m_completed;
    uint64_t m_nextDocument = 1;
    std::chrono::steady_clock::time_point m_healthAt;
};
} // namespace ft::db
