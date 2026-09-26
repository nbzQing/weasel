#include "WanxiangSchemeManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

std::filesystem::path test_user_directory;

namespace {
namespace fs = std::filesystem;

void Require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

void Write(const fs::path& path, const std::string& contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
  if (!output)
    throw std::runtime_error("unable to create fixture");
}

std::string Read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void Populate(const fs::path& user, const fs::path& stage) {
  Write(user / "wanxiang_lite.dict.yaml", "old managed");
  Write(user / "custom_phrase.txt", "user phrase");
  Write(user / "custom_phrase.dict.yaml", "personal phrase dictionary");
  Write(user / "weasel.custom.yaml", "user custom");
  Write(user / "custom" / "wanxiang_lite.custom.yaml", "user managed custom");
  Write(user / "my_dicts" / "personal.dict.yaml", "user dictionary");
  Write(user / "wanxiang_english.schema.yaml", "user non-lite schema");
  Write(user / "default.yaml", "user global settings");
  Write(stage / "wanxiang_lite.dict.yaml", "new managed");
  Write(stage / "dicts" / "added.lite.dict.yaml", "new file");
  Write(stage / "custom_phrase.txt", "upstream phrase");
  Write(stage / "custom_phrase.dict.yaml", "upstream phrase dictionary");
  Write(stage / "opencc" / "wanxiang_emoji.json", "new OpenCC data");
  Write(stage / "wanxiang_abbrev.schema.yaml", "new abbreviation schema");
  Write(stage / "weasel.custom.yaml", "upstream custom");
  Write(stage / "custom" / "wanxiang_lite.custom.yaml", "managed custom");
  Write(stage / "my_dicts" / "personal.dict.yaml", "upstream dictionary");
  Write(stage / "wanxiang_english.schema.yaml", "upstream non-lite schema");
  Write(stage / "default.yaml", "upstream global settings");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2, "fixture root argument missing");
    const fs::path root = fs::u8path(argv[1]);
    fs::remove_all(root);

    {
      const auto user = root / "commit-user";
      const auto stage = root / "commit-stage";
      Populate(user, stage);
      WanxiangSchemeManager manager(user);
      manager.installed_tag_ = L"v18.0.0";
      std::wstring error;
      Require(manager.InstallStaged(stage, &error), "scheme install failed");
      Require(Read(user / "wanxiang_lite.dict.yaml") == "new managed",
              "managed file was not replaced");
      Require(Read(user / "dicts" / "added.lite.dict.yaml") == "new file",
              "new managed file was not installed");
      Require(Read(user / "custom_phrase.txt") == "user phrase",
              "custom phrase was overwritten");
      Require(Read(user / "custom_phrase.dict.yaml") ==
                  "personal phrase dictionary",
              "personal phrase dictionary was overwritten");
      Require(
          Read(user / "opencc" / "wanxiang_emoji.json") == "new OpenCC data",
          "required OpenCC data was not installed");
      Require(Read(user / "wanxiang_abbrev.schema.yaml") ==
                  "new abbreviation schema",
              "required abbreviation schema was not installed");
      Require(Read(user / "weasel.custom.yaml") == "user custom",
              "root custom yaml was overwritten");
      Require(Read(user / "custom" / "wanxiang_lite.custom.yaml") ==
                  "managed custom",
              "package custom directory was not updated");
      Require(Read(user / "wanxiang_english.schema.yaml") ==
                  "upstream non-lite schema",
              "Lite dependency schema was not updated");
      Require(
          Read(user / "my_dicts" / "personal.dict.yaml") == "user dictionary",
          "user dictionary was overwritten");
      Require(Read(user / "default.yaml") == "user global settings",
              "global settings were overwritten");
      Require(manager.installed_,
              "successful install did not enter commit state");
      if (!manager.Commit(&error)) {
        std::wcerr << L"Commit error: " << error << L'\n';
        throw std::runtime_error("scheme transaction did not commit");
      }
      Require(!fs::exists(user / ".weasel-packages" / "scheme-transaction"),
              "committed transaction was not removed");
    }

    {
      const auto user = root / "bundled-user";
      const auto bundle = root / "bundled-data";
      Populate(user, bundle);
      Write(user / "version.txt", "18.0.9\n");
      Write(bundle / "version.txt", "18.0.11\n");
      Write(bundle / "wanxiang_lite.schema.yaml", "new Lite schema");
      Write(bundle / "lua" / "wanxiang" / "helper.lua", "new Lua data");
      Write(user / "opencc" / "hk2t.json", "personal OpenCC data");
      Write(bundle / "opencc" / "hk2t.json", "unrelated OpenCC data");
      WanxiangSchemeManager manager(user);
      bool updated = false;
      std::wstring error;
      Require(
          manager.InstallBundledIfNewer(bundle, &updated, &error) && updated,
          "newer bundled scheme was not installed");
      Require(Read(user / "version.txt") == "18.0.11\n",
              "bundled scheme version was not recorded");
      Require(Read(user / "wanxiang_lite.dict.yaml") == "new managed",
              "bundled managed dictionary was not updated");
      Require(Read(user / "lua" / "wanxiang" / "helper.lua") == "new Lua data",
              "bundled Lua data was not installed");
      Require(Read(user / "opencc" / "hk2t.json") == "personal OpenCC data",
              "unrelated OpenCC data was overwritten");
      Require(Read(user / "custom_phrase.dict.yaml") ==
                  "personal phrase dictionary",
              "bundled update overwrote personal phrases");
      updated = true;
      Require(
          manager.InstallBundledIfNewer(bundle, &updated, &error) && !updated,
          "equal bundled version was installed again");
    }

    {
      const auto user = root / "rollback-user";
      const auto stage = root / "rollback-stage";
      Populate(user, stage);
      WanxiangSchemeManager manager(user);
      std::wstring error;
      Require(manager.InstallStaged(stage, &error), "rollback setup failed");
      Require(manager.Rollback(&error), "scheme rollback failed");
      Require(Read(user / "wanxiang_lite.dict.yaml") == "old managed",
              "rollback did not restore replaced file");
      Require(!fs::exists(user / "dicts" / "added.lite.dict.yaml"),
              "rollback did not remove newly installed file");
      Require(Read(user / "custom_phrase.txt") == "user phrase",
              "rollback changed preserved file");
      Require(Read(user / "custom_phrase.dict.yaml") ==
                  "personal phrase dictionary",
              "rollback changed personal phrase dictionary");
      Require(Read(user / "custom" / "wanxiang_lite.custom.yaml") ==
                  "user managed custom",
              "rollback did not restore package custom file");
    }

    {
      const auto user = root / "recovery-user";
      const auto stage = root / "recovery-stage";
      Populate(user, stage);
      WanxiangSchemeManager installer(user);
      std::wstring error;
      Require(installer.InstallStaged(stage, &error), "recovery setup failed");
      WanxiangSchemeManager recovery(user);
      Require(recovery.RecoverInterruptedTransaction(&error),
              "interrupted transaction was not recovered");
      Require(Read(user / "wanxiang_lite.dict.yaml") == "old managed",
              "recovery did not restore replaced file");
      Require(!fs::exists(user / "dicts" / "added.lite.dict.yaml"),
              "recovery did not remove newly installed file");
    }

    fs::remove_all(root);
    std::cout << "Scheme transaction tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
