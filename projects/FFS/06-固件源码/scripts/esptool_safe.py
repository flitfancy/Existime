"""Run the installed PlatformIO esptool with normal Python dependencies."""
from pathlib import Path
import sys

package = Path.home() / '.platformio/packages/tool-esptoolpy'
sys.path.insert(0, str(package))
import esptool

if __name__ == '__main__':
    esptool._main()
