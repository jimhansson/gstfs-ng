/**
 *
 * Test-cases for the inotify replication functions
 *
 */

#include <gtest/gtest.h>



/**
 * this black box test should only be a driver and use a real gstfs
 * process, so we will set it up here in this fixture
 */
class black_box_inotify_test : public ::testing::Test {
private:
  /**
   * will on construction start the gstfs process and on destruction
   * kill it.
   */
  struct ProcessHolder {
    ProcessHolder() {
    }
    virtual ~ProcessHolder() {
    }
    int pid;
  };

public:
  virtual void SetUp() {
  }

  virtual void TearDown() {
  }
};



