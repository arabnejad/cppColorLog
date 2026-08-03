#include "cppColorLogger/logger.h"

#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

int multiTranslationUnitA();
int multiTranslationUnitB();

namespace {

class LoggerTest : public ::testing::Test {
protected:
  const std::string logFile = "test_log_output.txt";
  std::stringstream capturedCout;
  std::streambuf   *oldCout = nullptr;

  void SetUp() override {
    std::remove(logFile.c_str());
    LOGGER.pushLogSetting();
    LOGGER.setLogLevel(LogLevel::INFO);
    LOGGER.clearFilterLevels();
    oldCout = std::cout.rdbuf(capturedCout.rdbuf());
  }

  void TearDown() override {
    std::cout.rdbuf(oldCout);
    LOGGER.popLogSetting();
    std::remove(logFile.c_str());
  }

  std::string readFile() const {
    std::ifstream     file(logFile.c_str());
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }
};

class RecordingSink : public LogSink {
public:
  void write(const std::string &message) override {
    messages.push_back(message);
  }

  std::vector<std::string> messages;
};

class StructuredSink : public LogSink {
public:
  void write(const std::string &message) override {
    text = message;
  }

  void write(const LogEntry &entry) override {
    level = entry.level;
    color = entry.color;
    text  = entry.text;
  }

  LogLevel    level = LogLevel::ALWAYS;
  std::string color;
  std::string text;
};

class ReentrantSink : public LogSink {
public:
  explicit ReentrantSink(Logger &logger) : logger_(logger) {}

  void write(const std::string &) override {
    ++messageCount;
    if (messageCount == 1)
      logger_.log(LogLevel::INFO, "Nested message", "ReentrantSink");
  }

  int messageCount = 0;

private:
  Logger &logger_;
};

class ContextExample {
public:
  void emit() {
    LOGGER_C(LogLevel::INFO, "Class context");
  }
};

TEST_F(LoggerTest, LogsToFileWithoutColor) {
  const std::shared_ptr<FileSink> sink = LOGGER.addFileSink(logFile);
  ASSERT_TRUE(sink->isOpen());

  LOGGER_F(LogLevel::INFO, "File log test");

  const std::string content = readFile();
  EXPECT_NE(content.find("File log test"), std::string::npos);
  EXPECT_EQ(content.find("\033["), std::string::npos);
}

TEST_F(LoggerTest, LogLevelThresholdWorks) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::ERROR);

  LOGGER_F(LogLevel::DEBUG, "This should not appear");
  LOGGER_F(LogLevel::ERROR, "This should appear");

  const std::string content = readFile();
  EXPECT_EQ(content.find("This should not appear"), std::string::npos);
  EXPECT_NE(content.find("This should appear"), std::string::npos);
}

TEST_F(LoggerTest, FilterLevelWhitelistWorks) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::DEBUG);
  LOGGER.setFilterLevels({LogLevel::ERROR});

  LOGGER_F(LogLevel::DEBUG, "Filtered out");
  LOGGER_F(LogLevel::ERROR, "Filtered in");

  const std::string content = readFile();
  EXPECT_EQ(content.find("Filtered out"), std::string::npos);
  EXPECT_NE(content.find("Filtered in"), std::string::npos);
}

TEST_F(LoggerTest, PushPopSettingsRestoresConfiguration) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::INFO);
  LOGGER.pushLogSetting();
  LOGGER.setLogLevel(LogLevel::ERROR);

  LOGGER_F(LogLevel::INFO, "This should be hidden");
  LOGGER.popLogSetting();
  LOGGER_F(LogLevel::INFO, "This should be shown");

  const std::string content = readFile();
  EXPECT_EQ(content.find("This should be hidden"), std::string::npos);
  EXPECT_NE(content.find("This should be shown"), std::string::npos);
}

TEST_F(LoggerTest, ScopedSettingsRestoresConfiguration) {
  LOGGER.addFileSink(logFile);
  LOGGER.setLogLevel(LogLevel::INFO);
  {
    ScopedSettings temporary = LOGGER.scopedSettings();
    LOGGER.setLogLevel(LogLevel::ERROR);
    LOGGER_F(LogLevel::INFO, "Hidden in scope");
  }
  LOGGER_F(LogLevel::INFO, "Visible after scope");

  const std::string content = readFile();
  EXPECT_EQ(content.find("Hidden in scope"), std::string::npos);
  EXPECT_NE(content.find("Visible after scope"), std::string::npos);
}

TEST_F(LoggerTest, InMemorySinkCapturesLog) {
  LOGGER.enableInMemorySink();
  LOGGER_F(LogLevel::INFO, "Memory captured log");

  const std::vector<std::string> logs = LOGGER.getInMemoryLogs();
  ASSERT_FALSE(logs.empty());
  EXPECT_NE(logs.back().find("Memory captured log"), std::string::npos);
}

TEST_F(LoggerTest, InMemorySinkCanBeEnabledAgainAfterSettingsPop) {
  LOGGER.pushLogSetting();
  LOGGER.enableInMemorySink();
  LOGGER.popLogSetting();

  const std::shared_ptr<InMemorySink> sink = LOGGER.enableInMemorySink();
  LOGGER_F(LogLevel::INFO, "Captured after pop");

  ASSERT_TRUE(sink);
  ASSERT_FALSE(sink->getLogs().empty());
}

TEST_F(LoggerTest, ConsoleUsesConfiguredColor) {
  std::string customColor = Color::CYAN;
  LOGGER.setLevelColor(LogLevel::INFO, customColor);
  customColor.clear();
  LOGGER_F(LogLevel::INFO, "Custom color");

  const std::string output = capturedCout.str();
  EXPECT_NE(output.find(Color::CYAN), std::string::npos);
  EXPECT_NE(output.find(Color::RESET), std::string::npos);
  EXPECT_NE(output.find("Custom color"), std::string::npos);
}

TEST_F(LoggerTest, OutputContainsTimestampAndClassContext) {
  ContextExample example;
  example.emit();

  const std::string output = capturedCout.str();
  EXPECT_TRUE(std::regex_search(output, std::regex("\\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\]")));
  EXPECT_NE(output.find("ContextExample::emit]"), std::string::npos);
}

TEST(LoggerDesignTest, PublicCustomSinkReceivesMessages) {
  Logger                               logger(false);
  const std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>();
  logger.addSink(sink);

  logger.log(LogLevel::INFO, 42, "customSinkTest");

  ASSERT_EQ(sink->messages.size(), 1U);
  EXPECT_NE(sink->messages.front().find("42"), std::string::npos);
}

TEST(LoggerDesignTest, StructuredSinkReceivesLevelAndColor) {
  Logger                                logger(false);
  const std::shared_ptr<StructuredSink> sink = std::make_shared<StructuredSink>();
  logger.addSink(sink);
  logger.setLevelColor(LogLevel::WARN, Color::BLUE);

  logger.log(LogLevel::WARN, "Structured message", "structuredSinkTest");

  EXPECT_EQ(sink->level, LogLevel::WARN);
  EXPECT_EQ(sink->color, Color::BLUE);
  EXPECT_NE(sink->text.find("Structured message"), std::string::npos);
}

TEST(LoggerDesignTest, NestedSettingsRestoreInOrder) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  logger.pushLogSetting();
  logger.setLogLevel(LogLevel::ERROR);
  logger.log(LogLevel::INFO, "Hidden outer", "nestedSettingsTest");

  logger.pushLogSetting();
  logger.setLogLevel(LogLevel::DEBUG);
  logger.log(LogLevel::INFO, "Visible inner", "nestedSettingsTest");
  logger.popLogSetting();

  logger.log(LogLevel::INFO, "Hidden outer again", "nestedSettingsTest");
  logger.popLogSetting();
  logger.log(LogLevel::INFO, "Visible restored", "nestedSettingsTest");

  const std::vector<std::string> logs = sink->getLogs();
  ASSERT_EQ(logs.size(), 2U);
  EXPECT_NE(logs[0].find("Visible inner"), std::string::npos);
  EXPECT_NE(logs[1].find("Visible restored"), std::string::npos);
}

TEST(LoggerDesignTest, EmptySettingsPopIsHarmless) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  logger.popLogSetting();
  logger.log(LogLevel::INFO, "Still configured", "emptyPopTest");

  EXPECT_EQ(sink->getLogs().size(), 1U);
}

TEST(LoggerDesignTest, AlwaysAndVerboseThresholdSemanticsArePreserved) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();

  logger.setLogLevel(LogLevel::FATAL);
  logger.log(LogLevel::ALWAYS, "Always passes threshold", "levelTest");
  logger.log(LogLevel::VERBOSE, "Verbose hidden", "levelTest");
  logger.setLogLevel(LogLevel::VERBOSE);
  logger.log(LogLevel::VERBOSE, "Verbose visible", "levelTest");

  ASSERT_EQ(sink->getLogs().size(), 2U);
  logger.setFilterLevels({LogLevel::ERROR});
  logger.log(LogLevel::ALWAYS, "Always filtered by whitelist", "levelTest");
  EXPECT_EQ(sink->getLogs().size(), 2U);
}

TEST(LoggerDesignTest, ReentrantCustomSinkDoesNotDeadlock) {
  Logger                               logger(false);
  const std::shared_ptr<ReentrantSink> sink = std::make_shared<ReentrantSink>(logger);
  logger.addSink(sink);

  logger.log(LogLevel::INFO, "Outer message", "reentrantTest");

  EXPECT_EQ(sink->messageCount, 2);
}

TEST(LoggerDesignTest, OriginalLogLevelNameRemainsCompatible) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.log(LOGLEVELL::INFO, "Compatibility", "compatibilityTest");

  EXPECT_EQ(sink->getLogs().size(), 1U);
}

TEST(LoggerDesignTest, MemorySinkIsSafeForConcurrentLogging) {
  Logger                              logger(false);
  const std::shared_ptr<InMemorySink> sink = logger.enableInMemorySink();
  logger.setLogLevel(LogLevel::VERBOSE);

  const int                threadCount       = 8;
  const int                messagesPerThread = 100;
  std::vector<std::thread> threads;
  for (int thread = 0; thread < threadCount; ++thread) {
    threads.push_back(std::thread([&logger, messagesPerThread]() {
      for (int message = 0; message < messagesPerThread; ++message)
        logger.log(LogLevel::DEBUG, message, "worker");
    }));
  }
  for (std::vector<std::thread>::iterator thread = threads.begin(); thread != threads.end(); ++thread)
    thread->join();

  EXPECT_EQ(sink->getLogs().size(), static_cast<std::size_t>(threadCount * messagesPerThread));
}

TEST(LoggerDesignTest, HeaderLinksAcrossTranslationUnits) {
  EXPECT_EQ(multiTranslationUnitA(), 1);
  EXPECT_EQ(multiTranslationUnitB(), 1);
}

} // namespace
