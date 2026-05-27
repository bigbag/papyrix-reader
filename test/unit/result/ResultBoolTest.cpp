#include "test_utils.h"

#include "Result.h"

namespace papyrix {
const char* errorToString(Error) { return ""; }
}  // namespace papyrix

int main() {
  TestUtils::TestRunner runner("ResultBool");

  // Result<bool> with Ok(true) should be truthy via ok() and have true value
  {
    auto r = papyrix::Ok(true);
    runner.expectTrue(r.ok(), "Ok(true): ok() is true");
    runner.expectTrue(*r, "Ok(true): value is true");
    runner.expectTrue(static_cast<bool>(r), "Ok(true): operator bool is true");
  }

  // Result<bool> with Ok(false) — ok() is true but value is false
  // This is the bug scenario: using operator bool() instead of checking value
  {
    auto r = papyrix::Ok(false);
    runner.expectTrue(r.ok(), "Ok(false): ok() is true");
    runner.expectTrue(!*r, "Ok(false): value is false");
    runner.expectTrue(static_cast<bool>(r), "Ok(false): operator bool is true (checks ok, not value!)");
  }

  // Result<bool> with error — both ok() and value are falsy
  {
    auto r = papyrix::Err<bool>(papyrix::Error::FileNotFound);
    runner.expectTrue(!r.ok(), "Err: ok() is false");
    runner.expectTrue(!static_cast<bool>(r), "Err: operator bool is false");
  }

  // Correct pattern for checking file existence: ok() && *result
  {
    auto exists = papyrix::Ok(true);
    runner.expectTrue(exists.ok() && *exists, "exists(true): correct check passes");
  }

  {
    auto notExists = papyrix::Ok(false);
    runner.expectTrue(!(notExists.ok() && *notExists), "exists(false): correct check fails");
  }

  {
    auto err = papyrix::Err<bool>(papyrix::Error::SdCardNotFound);
    runner.expectTrue(!(err.ok() && *err), "exists(error): correct check fails");
  }

  // Result<void>
  {
    auto ok = papyrix::Ok();
    runner.expectTrue(ok.ok(), "void Ok: ok() is true");
    runner.expectTrue(static_cast<bool>(ok), "void Ok: operator bool is true");
  }

  {
    auto err = papyrix::ErrVoid(papyrix::Error::ParseFailed);
    runner.expectTrue(!err.ok(), "void Err: ok() is false");
    runner.expectTrue(!static_cast<bool>(err), "void Err: operator bool is false");
  }

  return runner.allPassed() ? 0 : 1;
}
