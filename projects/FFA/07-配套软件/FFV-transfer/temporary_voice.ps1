param([Parameter(Mandatory=$true)][string]$TextPath,
      [Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$voice = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $voice.SelectVoice('Microsoft Huihui Desktop')
    $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(
        48000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
        [System.Speech.AudioFormat.AudioChannel]::Mono)
    $voice.SetOutputToWaveFile($OutputPath, $format)
    $voice.Speak([System.IO.File]::ReadAllText($TextPath, [System.Text.Encoding]::UTF8))
} finally {
    $voice.Dispose()
}
