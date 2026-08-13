void UnusedHelper() {
  int nNever = 1;
  PrintString("never");
}

void main() {
  int nUnused = 1;
  int nAlsoUnused = nUnused + 2;
  PrintString("hi");
}
