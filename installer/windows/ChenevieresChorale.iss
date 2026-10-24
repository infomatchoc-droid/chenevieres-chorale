; Installateur Windows de Chenevieres Chorale (Inno Setup 6)

#define AppName "Chenevieres Chorale"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{553C3B2F-FC2C-48A5-9773-7F69DA261751}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Studio des Chenevieres
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=Chenevieres-Chorale-{#AppVersion}-Windows
OutputDir=..\..\dist
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
WizardImageFile=..\branding\wizard-large-100.bmp,..\branding\wizard-large-125.bmp,..\branding\wizard-large-150.bmp,..\branding\wizard-large-200.bmp
WizardSmallImageFile=..\branding\wizard-small-100.bmp,..\branding\wizard-small-125.bmp,..\branding\wizard-small-150.bmp,..\branding\wizard-small-200.bmp
WizardImageStretch=no
SetupIconFile=chenevieres.ico
UninstallDisplayIcon={uninstallexe}
ShowLanguageDialog=auto
UninstallDisplayName={#AppName} (VST3, AAX)
UninstallFilesDir={commonpf64}\Studio des Chenevieres\{#AppName}

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
french.WelcomeLabel1=Bienvenue dans l'installation de Chènevières Chorale
french.WelcomeLabel2=Merci d'avoir choisi Chènevières Chorale, le chœur virtuel du Studio des Chènevières : une voix entre, un chœur de 3 à 1000 choristes en sort.%n%nL'installation copie le plugin VST3 (et AAX pour Pro Tools). Fermez votre logiciel audio avant de continuer.%n%nCliquez sur Suivant pour commencer.
french.FinishedHeadingLabel=C'est installé, merci !
french.FinishedLabel=Chènevières Chorale est prêt. Ouvrez votre logiciel audio et relancez la recherche des plugins si besoin.%n%nBonne musique,%nMathieu — Studio des Chènevières
english.WelcomeLabel1=Welcome to the Chènevières Chorale setup
english.WelcomeLabel2=Thank you for choosing Chènevières Chorale, the virtual choir from Studio des Chènevières: one voice in, a choir of 3 to 1000 singers out.%n%nSetup installs the VST3 plug-in (and AAX for Pro Tools). Please close your audio software before continuing.%n%nClick Next to start.
english.FinishedHeadingLabel=All set, thank you!
english.FinishedLabel=Chènevières Chorale is ready. Open your audio software and rescan your plug-ins if needed.%n%nEnjoy the music,%nMathieu — Studio des Chènevières

[Components]
Name: "vst3"; Description: "VST3"; Types: full compact custom; Flags: fixed
Name: "aax"; Description: "AAX (Pro Tools)"; Types: full custom

[Files]
Source: "..\..\build\ChenevieresChorale_artefacts\Release\VST3\Chenevieres Chorale.vst3\*"; DestDir: "{commoncf64}\VST3\Chenevieres Chorale.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\ChenevieresChorale_artefacts\Release\AAX\Chenevieres Chorale.aaxplugin\*"; DestDir: "{commoncf64}\Avid\Audio\Plug-Ins\Chenevieres Chorale.aaxplugin"; Components: aax; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Chenevieres Chorale.vst3"
Type: filesandordirs; Name: "{commoncf64}\Avid\Audio\Plug-Ins\Chenevieres Chorale.aaxplugin"
