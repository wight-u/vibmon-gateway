#pragma once
#include <atomic>
#include <string>
#include <thread>

#include "protocol.hpp"
#include "ring_buffer.hpp"

struct sqlite3; // forward-declare to avoid including sqlite3.h in the header

namespace vibmon {

// Background thread that drains the ring buffer and persists samples to SQLite.
//
// Schema:
//   CREATE TABLE samples(ts INTEGER, ax REAL, ay REAL, az REAL,
//                        gx REAL, gy REAL, gz REAL)
//
// Writes in batches (up to 256 samples per transaction) for throughput.
// Prunes rows older than 24 h every ~60 s to bound database size.
// Uses WAL mode so reads (REST API) never block writes.
class DbWriter {
  public:
    explicit DbWriter(RingBuffer<ImuSample, 2048>& ring,
                      std::string                  db_path = "/var/lib/vibmon/samples.db");
    ~DbWriter();

    DbWriter(const DbWriter&)            = delete;
    DbWriter& operator=(const DbWriter&) = delete;

    void start();
    void stop();

    uint64_t rows_written() const noexcept { return rows_written_.load(); }

    // Read the most recent `limit` samples from SQLite into out[].
    // Returns the number of rows actually read. Thread-safe (uses a
    // separate read connection).
    int read_latest(ImuSample* out, int limit) const;

  private:
    void run();
    void prune_old_rows();
    bool open_db();

    RingBuffer<ImuSample, 2048>& ring_;
    std::string                  db_path_;
    sqlite3*                     db_ = nullptr;
    std::atomic<bool>            running_{false};
    std::thread                  thread_;
    std::atomic<uint64_t>        rows_written_{0};
    uint64_t                     consumer_seq_ = 0;
};

} // namespace vibmon
