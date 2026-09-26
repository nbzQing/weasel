#include <WeaselPaletteCatalog.h>
#include <iostream>

int main() {
  auto* rime = rime_get_api();
  weasel::PaletteCatalog catalog;
  const bool loaded = catalog.LoadFiles(
      rime, L"C:\\Program Files\\Rime\\weasel-0.17.4\\data", L"D:\\RIME");
  std::cout << "loaded=" << loaded << " schemes=" << catalog.schemes().size()
            << " groups=" << catalog.groups().size() << '\n';
  return loaded ? 0 : 1;
}
