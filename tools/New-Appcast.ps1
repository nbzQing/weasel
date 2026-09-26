param(
  [Parameter(Mandatory = $true)][string]$Version,
  [Parameter(Mandatory = $true)][string]$InstallerName,
  [Parameter(Mandatory = $true)][long]$InstallerSize,
  [string]$OutputPath = (Join-Path $PSScriptRoot '..\output\archives\appcast.xml')
)

$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^0\.17\.4\.[0-9]+$') {
  throw 'The appcast version must be a four-part Weasel version.'
}
if ($InstallerName -notmatch '^weasel-official-0\.17\.4-custom-(0\.17\.4\.[0-9]+)(?:\.[0-9a-f]{7,40})?-installer\.exe$' -or
    $Matches[1] -ne $Version -or
    $InstallerSize -le 0) {
  throw 'The appcast installer name, version, or size is invalid.'
}

$releaseUrl = "https://github.com/nbzQing/weasel/releases/tag/$Version"
$installerUrl = "https://github.com/nbzQing/weasel/releases/download/$Version/$InstallerName"
$published = [DateTimeOffset]::UtcNow.ToString('ddd, dd MMM yyyy HH:mm:ss +0000',
  [Globalization.CultureInfo]::InvariantCulture)
$xml = @"
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>Weasel Custom Updates</title>
    <link>https://github.com/nbzQing/weasel/releases/latest/download/appcast.xml</link>
    <description>Weasel Custom installer updates</description>
    <language>zh-CN</language>
    <item>
      <title>Weasel Custom $Version</title>
      <sparkle:releaseNotesLink>$releaseUrl</sparkle:releaseNotesLink>
      <pubDate>$published</pubDate>
      <enclosure url="$installerUrl" sparkle:version="$Version" length="$InstallerSize" type="application/octet-stream" />
    </item>
  </channel>
</rss>
"@
$document = [xml]$xml
$enclosure = $document.SelectSingleNode('/rss/channel/item/enclosure')
if (-not $enclosure -or $enclosure.GetAttribute('url') -ne $installerUrl -or
    $enclosure.GetAttribute('length') -ne [string]$InstallerSize -or
    $enclosure.GetAttribute('version',
      'http://www.andymatuschak.org/xml-namespaces/sparkle') -ne $Version) {
  throw 'The generated appcast is invalid.'
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) |
  Out-Null
[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputPath), $xml + "`n",
  [Text.UTF8Encoding]::new($false))
Write-Host "Created appcast for $Version and $InstallerName"
