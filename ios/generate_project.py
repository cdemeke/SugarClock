#!/usr/bin/env python3
"""Deterministic dependency-free Xcode project generator; no signing identity assumed."""
from pathlib import Path
import hashlib
root=Path(__file__).resolve().parent
files=sorted((root/'SugarClock').rglob('*.swift'))
def uid(s):return hashlib.sha256(s.encode()).hexdigest()[:24].upper()
objects=[]
def obj(name,body):objects.append(f'{uid(name)} = {{ {body} }};');return uid(name)
builds=[];refs=[]
for file in files:
 path=str(file.relative_to(root));ref=obj(path,f'isa = PBXFileReference; lastKnownFileType = sourcecode.swift; path = "{path}"; sourceTree = SOURCE_ROOT;');refs.append(ref)
 builds.append(obj(path+'build',f'isa = PBXBuildFile; fileRef = {ref};'))
asset=obj('assets','isa = PBXFileReference; lastKnownFileType = folder.assetcatalog; path = SugarClock/Assets.xcassets; sourceTree = SOURCE_ROOT;')
refs.append(asset)
assetBuild=obj('assetsBuild',f'isa = PBXBuildFile; fileRef = {asset};')
privacy=obj('privacy','isa = PBXFileReference; lastKnownFileType = text.xml; path = SugarClock/PrivacyInfo.xcprivacy; sourceTree = SOURCE_ROOT;')
refs.append(privacy)
privacyBuild=obj('privacyBuild',f'isa = PBXBuildFile; fileRef = {privacy};')
product=obj('product' ,'isa = PBXFileReference; explicitFileType = wrapper.application; path = SugarClock.app; sourceTree = BUILT_PRODUCTS_DIR;')
products=obj('products',f'isa = PBXGroup; children = ({product},); name = Products; sourceTree = "<group>";')
group=obj('group',f'isa = PBXGroup; children = ({",".join(refs)},{products},); sourceTree = "<group>";')
sources=obj('sources',f'isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = ({",".join(builds)},); runOnlyForDeploymentPostprocessing = 0;')
frameworks=obj('frameworks','isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = (); runOnlyForDeploymentPostprocessing = 0;')
resources=obj('resources',f'isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = ({assetBuild},{privacyBuild},); runOnlyForDeploymentPostprocessing = 0;')
configs=[]
for config in ('Debug','Release'):
 configs.append(obj(config,'''isa = XCBuildConfiguration; name = '''+config+'''; buildSettings = {
 SUPPORTED_PLATFORMS = "iphoneos iphonesimulator"; ALWAYS_SEARCH_USER_PATHS = NO; SDKROOT = iphoneos; IPHONEOS_DEPLOYMENT_TARGET = 17.0; TARGETED_DEVICE_FAMILY = "1,2";
 SWIFT_VERSION = 5.0; PRODUCT_NAME = SugarClock; PRODUCT_BUNDLE_IDENTIFIER = com.sugarclock.companion;
 GENERATE_INFOPLIST_FILE = YES; ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;
 INFOPLIST_KEY_NSBluetoothAlwaysUsageDescription = "SugarClock uses Bluetooth to securely pair with and configure your nearby clocks.";
 INFOPLIST_KEY_UILaunchScreen_Generation = YES; INFOPLIST_KEY_UIApplicationSceneManifest_Generation = YES;
 INFOPLIST_KEY_CFBundleDisplayName = SugarClock; MARKETING_VERSION = 1.0.0; CURRENT_PROJECT_VERSION = 1;
 SWIFT_ACTIVE_COMPILATION_CONDITIONS = "'''+('DEBUG' if config=='Debug' else '')+'''";
 CODE_SIGN_STYLE = Automatic; ENABLE_USER_SCRIPT_SANDBOXING = YES; SWIFT_OPTIMIZATION_LEVEL = "'''+('-Onone' if config=='Debug' else '-O')+'''";
 };'''.replace('','')))
cl=obj('configs',f'isa = XCConfigurationList; buildConfigurations = ({",".join(configs)},); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release;')
target=obj('target',f'isa = PBXNativeTarget; buildConfigurationList = {cl}; buildPhases = ({sources},{frameworks},{resources},); buildRules = (); dependencies = (); name = SugarClock; productName = SugarClock; productReference = {product}; productType = "com.apple.product-type.application";')
project=obj('project',f'isa = PBXProject; attributes = {{ LastUpgradeCheck = 2600; }}; buildConfigurationList = {cl}; compatibilityVersion = "Xcode 14.0"; developmentRegion = en; hasScannedForEncodings = 0; knownRegions = (en,Base,); mainGroup = {group}; productRefGroup = {products}; projectDirPath = ""; projectRoot = ""; targets = ({target},);')
out=root/'SugarClock.xcodeproj';out.mkdir(exist_ok=True)
(out/'project.pbxproj').write_text('// !$*UTF8*$!\n{ archiveVersion = 1; classes = {}; objectVersion = 56; objects = {\n'+'\n'.join(objects)+f'\n}}; rootObject = {project}; }}\n')
scheme=out/'xcshareddata/xcschemes';scheme.mkdir(parents=True,exist_ok=True)
(scheme/'SugarClock.xcscheme').write_text(f'''<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion="2600" version="1.3"><BuildAction parallelizeBuildables="YES" buildImplicitDependencies="YES"><BuildActionEntries><BuildActionEntry buildForTesting="YES" buildForRunning="YES" buildForProfiling="YES" buildForArchiving="YES" buildForAnalyzing="YES"><BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="{target}" BuildableName="SugarClock.app" BlueprintName="SugarClock" ReferencedContainer="container:SugarClock.xcodeproj"/></BuildActionEntry></BuildActionEntries></BuildAction><LaunchAction buildConfiguration="Debug" selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier="Xcode.IDEFoundation.Launcher.LLDB" launchStyle="0" useCustomWorkingDirectory="NO" ignoresPersistentStateOnLaunch="NO" debugDocumentVersioning="YES" debugServiceExtension="internal" allowLocationSimulation="YES"><BuildableProductRunnable runnableDebuggingMode="0"><BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="{target}" BuildableName="SugarClock.app" BlueprintName="SugarClock" ReferencedContainer="container:SugarClock.xcodeproj"/></BuildableProductRunnable></LaunchAction><ArchiveAction buildConfiguration="Release" revealArchiveInOrganizer="YES"/></Scheme>''')
