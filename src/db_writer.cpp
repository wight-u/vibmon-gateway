#include "db_writer.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include <sqlite3.h>

namespace vibmon {

static constexpr int  BATCH_SIZE    = 256;
static constexpr int  SLEEP_MS      = 10; // poll interval when ring is empty
static constexpr long PRUNE_EVERY_S = 60;
static constexpr long RETENTION_MS  = 24LL * 3600 * 1000;

DbWriter::DbWriter(RingBuffer<ImuSample, 2048>& ring, std::string db_path)
    : ring_(ring), db_path_(std::move(db_path)) {}

DbWriter::~DbWriter() {
    stop();
}

bool DbWriter::open_db() {
    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
        std::fprintf(stderr, "db_writer: sqlite3_open: %s — persistence disabled\n",
                     sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA cache_size=-4096;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_,
                 "CREATE TABLE IF NOT EXISTS samples("
                 "  ts INTEGER NOT NULL,"
                 "  ax REAL, ay REAL, az REAL,"
                 "  gx REAL, gy REAL, gz REAL"
                 ");",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "CREATE INDEX IF NOT EXISTS idx_ts ON samples(ts);", nullptr, nullptr,
                 nullptr);
    return true;
}

void DbWriter::start() {
    if (!open_db())
        return;
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&DbWriter::run, this);
}

void DbWriter::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable())
        thread_.join();
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void DbWriter::run() {
    ImuSample batch[BATCH_SIZE];
    auto      last_prune = std::chrono::steady_clock::now();

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO samples(ts,ax,ay,az,gx,gy,gz) VALUES(?,?,?,?,?,?,?);", -1,
                       &stmt, nullptr);

    while (running_.load(std::memory_order_acquire)) {
        const std::size_t n = ring_.drain(consumer_seq_, batch, BATCH_SIZE);

        if (n > 0) {
            sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);
            for (std::size_t i = 0; i < n; ++i) {
                const auto& s = batch[i];
                sqlite3_bind_int64(stmt, 1, s.ts_ms);
                sqlite3_bind_double(stmt, 2, s.ax);
                sqlite3_bind_double(stmt, 3, s.ay);
                sqlite3_bind_double(stmt, 4, s.az);
                sqlite3_bind_double(stmt, 5, s.gx);
                sqlite3_bind_double(stmt, 6, s.gy);
                sqlite3_bind_double(stmt, 7, s.gz);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
            rows_written_.fetch_add(n, std::memory_order_relaxed);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_MS));
        }

        // Prune rows older than 24 h
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_prune).count() >=
            PRUNE_EVERY_S) {
            prune_old_rows();
            last_prune = now;
        }
    }

    sqlite3_finalize(stmt);
}

void DbWriter::prune_old_rows() {
    using namespace std::chrono;
    const int64_t cutoff =
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - RETENTION_MS;
    sqlite3_exec(db_, ("DELETE FROM samples WHERE ts < " + std::to_string(cutoff) + ";").c_str(),
                 nullptr, nullptr, nullptr);
}

int DbWriter::read_latest(ImuSample* out, int limit) const {
    sqlite3* rdb = nullptr;
    if (sqlite3_open_v2(db_path_.c_str(), &rdb, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        return 0;

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(rdb, "SELECT ts,ax,ay,az,gx,gy,gz FROM samples ORDER BY ts DESC LIMIT ?;",
                       -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);

    int row = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && row < limit) {
        auto& s = out[row++];
        s.ts_ms = sqlite3_column_int64(stmt, 0);
        s.ax    = static_cast<float>(sqlite3_column_double(stmt, 1));
        s.ay    = static_cast<float>(sqlite3_column_double(stmt, 2));
        s.az    = static_cast<float>(sqlite3_column_double(stmt, 3));
        s.gx    = static_cast<float>(sqlite3_column_double(stmt, 4));
        s.gy    = static_cast<float>(sqlite3_column_double(stmt, 5));
        s.gz    = static_cast<float>(sqlite3_column_double(stmt, 6));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(rdb);

    // Results come out newest-first; reverse to get chronological order
    for (int lo = 0, hi = row - 1; lo < hi; ++lo, --hi)
        std::swap(out[lo], out[hi]);

    return row;
}

} // namespace vibmon
