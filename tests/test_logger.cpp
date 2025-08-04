#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include "cppColorLogger/logger.h"


class LoggerTest : public ::testing::Test {
protected:
    const std::string logFile = "test_log_output.txt";
    std::stringstream capturedCout;
    std::streambuf* oldCout = nullptr;

    void SetUp() override {
        std::remove(logFile.c_str());
        LOGGER.setLogLevel(LOGLEVELL::INFO);
        LOGGER.clearFilterLevels();

        // Redirect std::cout to capturedCout
        oldCout = std::cout.rdbuf(capturedCout.rdbuf());
    }

    void TearDown() override {
        // Restore std::cout
        std::cout.rdbuf(oldCout);
    }

    std::string readFile() {
        std::ifstream file(logFile);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    void enableFileSink() {
        LOGGER.setFileOutput(logFile);
    }

    std::string getCapturedOutput() {
        return capturedCout.str();
    }
};



TEST_F(LoggerTest, LogsToFileWithoutColor) {
    enableFileSink();
    LOGGER_F(LOGLEVELL::INFO, "File log test");
    std::string content = readFile();
    EXPECT_NE(content.find("File log test"), std::string::npos);
    EXPECT_EQ(content.find("\033["), std::string::npos);  // No ANSI codes
}

TEST_F(LoggerTest, LogLevelFilteringWorks) {
    enableFileSink();
    LOGGER.setLogLevel(LOGLEVELL::ERROR);
    LOGGER_F(LOGLEVELL::DEBUG, "This should not appear");
    LOGGER_F(LOGLEVELL::ERROR, "This should appear");
    std::string content = readFile();
    EXPECT_EQ(content.find("This should not appear"), std::string::npos);
    EXPECT_NE(content.find("This should appear"), std::string::npos);
}

TEST_F(LoggerTest, FilterLevelWhitelist) {
    enableFileSink();
    LOGGER.setLogLevel(LOGLEVELL::DEBUG);
    LOGGER.setFilterLevels({ LOGLEVELL::ERROR });
    LOGGER_F(LOGLEVELL::DEBUG, "Filtered out");
    LOGGER_F(LOGLEVELL::ERROR, "Filtered in");
    std::string content = readFile();
    EXPECT_EQ(content.find("Filtered out"), std::string::npos);
    EXPECT_NE(content.find("Filtered in"), std::string::npos);
}

TEST_F(LoggerTest, PushPopSettings) {
    enableFileSink();
    LOGGER.setLogLevel(LOGLEVELL::INFO);
    LOGGER.pushLogSetting();
    LOGGER.setLogLevel(LOGLEVELL::ERROR);
    LOGGER_F(LOGLEVELL::INFO, "This should be hidden");
    LOGGER.popLogSetting();
    LOGGER_F(LOGLEVELL::INFO, "This should be shown");
    std::string content = readFile();
    EXPECT_EQ(content.find("This should be hidden"), std::string::npos);
    EXPECT_NE(content.find("This should be shown"), std::string::npos);
}

TEST_F(LoggerTest, InMemorySinkCapturesLog) {
    LOGGER.enableInMemorySink();
    LOGGER_F(LOGLEVELL::INFO, "Memory captured log");
    const auto& logs = LOGGER.getInMemoryLogs();
    bool found = false;
    for (const auto& entry : logs) {
        if (entry.find("Memory captured log") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(LoggerTest, ConsoleColorOutputViaLogger) {
  // Temporarily replace std::cout with a buffer
  std::stringstream buffer;
  std::streambuf   *original = std::cout.rdbuf(buffer.rdbuf());

  // Log an error
  LOGGER.setLogLevel(LOGLEVELL::ERROR);
  LOGGER_F(LOGLEVELL::ERROR, "Error with color");

  // Restore std::cout
  std::cout.rdbuf(original);

  std::string output = buffer.str();
  EXPECT_NE(output.find("\033[31m"), std::string::npos); // Expect red
  EXPECT_NE(output.find("\033[0m"), std::string::npos);  // Expect reset
  EXPECT_NE(output.find("Error with color"), std::string::npos);
}
