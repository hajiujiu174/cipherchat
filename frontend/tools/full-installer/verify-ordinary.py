from pathlib import Path
source=Path(__file__).with_name('verify_bootstrap.py')
exec(compile(source.read_text(encoding='utf-8').replace('0.3.14.5','0.3.15.0'),str(source),'exec'))
