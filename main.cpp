#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <windows.h> 
#include <vector>
#include <algorithm> 
#include <processthreadsapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <random> 
#include <iomanip> 
#include <cctype> 
#include <io.h>      
#include <fcntl.h>   

using namespace std;

// --- 定数定義 (省略) ---
const string VHDX_FILE = "C:\\UEFIBoot.vhdx";
const string DRIVE_LETTER = "Y";
const string BOOT_FILE_NAME = "BOOTX64.efi";
const string ZIP_SHELL_PATH = "EFI\\BOOT\\" + BOOT_FILE_NAME;
const string STARTUP_NSH_FILENAME = "startup.nsh";
const string BOOT_ENTRY_NAME = "UEFI BIOS Edit Shell (VHDX)";
const string DISKPART_SCRIPT = "diskpart_script.txt";
const string ZIP_FILENAME = "BiosEdit.zip";
const string CLEANUP_EXE_FILENAME = "cleanup.exe";
const string CLEANUP_SERVICE_NAME = "CleanupService";
const string STAGING_DIR = "C:\\TEMP_UEFI_BOOT\\";

// --- グローバル変数: 実行ファイルのディレクトリ (省略) ---
string g_CurrentDir;

// --- 関数: get_full_path (省略) ---
string get_full_path(const string& filename) {
    if (g_CurrentDir.empty()) {
        char buffer[MAX_PATH];
        DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
        if (length > 0) {
            string full_path = buffer;
            size_t last_slash = full_path.find_last_of("\\/");
            if (last_slash != string::npos) {
                g_CurrentDir = full_path.substr(0, last_slash + 1);
            }
        }
    }
    return g_CurrentDir + filename;
}

// --- 関数: generate_random_hex (省略) ---
string generate_random_hex(size_t length) {
    static random_device rd;
    static mt19937 generator(rd());

    uniform_int_distribution<int> distribution(0, 15);

    stringstream ss;
    for (size_t i = 0; i < length; ++i) {
        ss << hex << distribution(generator);
    }

    string result = ss.str();

    for (char& c : result) {
        if (c >= 'a' && c <= 'f') {
            c = c - 'a' + 'A';
        }
    }

    return result;
}

// --- 関数: check_admin_rights (省略) ---
bool check_admin_rights() {
    if (IsUserAnAdmin()) {
        return true;
    }
    wcerr << L"\n================================================================" << endl;
    wcerr << L"? エラー: このプログラムは管理者権限で実行されていません。" << endl;
    wcerr << L"   プログラムを右クリックし、「管理者として実行」を選択してください。" << endl;
    wcerr << L"================================================================" << endl;
    return false;
}

// --- 関数: check_secure_boot_status (省略) ---
bool check_secure_boot_status() {
    HKEY hKey;
    DWORD dwValue = 0;
    DWORD dwSize = sizeof(dwValue);

    LONG result = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
        0,
        KEY_READ,
        &hKey
    );

    if (result == ERROR_SUCCESS) {
        result = RegQueryValueExA(
            hKey,
            "UEFISecureBootEnabled",
            NULL,
            NULL,
            (LPBYTE)&dwValue,
            &dwSize
        );
        RegCloseKey(hKey);

        if (result == ERROR_SUCCESS) {
            if (dwValue == 0) {
                return true;
            }
            wcerr << L"\n================================================================" << endl;
            wcerr << L"? 警告/エラー: Secure Boot (セキュアブート) が有効です。" << endl;
            wcerr << L"   このプログラムは**未署名**のUEFI Shellを使用するため、Secure Bootを" << endl;
            wcerr << L"   **無効**にしないとWindowsが起動します (元のエラーに戻ります)。" << endl;
            wcerr << L"   BIOS/UEFI設定画面でSecure Bootを無効にしてから再度実行してください。" << endl;
            return false;
        }
    }

    wcerr << L"\n================================================================" << endl;
    wcerr << L"?? 警告: Secure Bootの状態を確認できませんでした。または有効です。" << endl;
    wcerr << L"   VHDXブートのためには、Secure Bootを無効にする必要があります。" << endl;
    return false;
}

// --- 関数: cleanup_old_boot_entries (省略) ---
void cleanup_old_boot_entries() {
    wcout << L"\n--- 0. 古いブートエントリーの削除 (クリーンアップ) ---" << endl;

    const string LIST_FILE = "bcd_list.txt";
    system(("bcdedit /enum all > " + LIST_FILE).c_str());

    ifstream file(LIST_FILE);
    string line;
    string current_guid = "";
    vector<string> guids_to_delete;

    while (getline(file, line)) {
        if (line.find("identifier") != string::npos) {
            size_t start = line.find('{');
            size_t end = line.find('}');
            if (start != string::npos && end != string::npos) {
                current_guid = line.substr(start, end - start + 1);
            }
            else {
                current_guid = "";
            }
        }
        else if (!current_guid.empty() && line.find("description") != string::npos) {
            if (line.find(BOOT_ENTRY_NAME) != string::npos) {
                guids_to_delete.push_back(current_guid);
                current_guid = "";
            }
        }
    }
    file.close();
    std::remove(LIST_FILE.c_str());

    system(("sc delete " + CLEANUP_SERVICE_NAME + " > NUL 2>&1").c_str());

    if (guids_to_delete.empty()) {
        wcout << L"  - ? 削除対象の古いエントリーは見つかりませんでした。" << endl;
    }
    else {
        wcout << L"* 古いエントリー (" << guids_to_delete.size() << L"件) を削除します..." << endl;
        for (const string& guid : guids_to_delete) {
            if (system(("bcdedit /delete " + guid + " /f").c_str()) == 0) {
                wcout << L"  - 削除済み: " << guid.c_str() << endl;
            }
            else {
                wcerr << L"  - ? 削除失敗: " << guid.c_str() << endl;
            }
        }
        wcout << L"  - ? 古いブートエントリーのクリーンアップ完了。" << endl;
    }
}


// --- 関数: diskpartスクリプトを生成・実行する (GPT変換/デバッグ強化済み) ---
void setup_vhdx_disk() {
    wcout << L"\n--- 1. VHDXの作成とYドライブ割り当て (FAT32フォーマット) ---" << endl;

    string detach_command = "diskpart /s " + DISKPART_SCRIPT + " > NUL 2>&1";
    {
        // 既存VHDXのデタッチ/削除を試行
        ofstream cleanup_script(DISKPART_SCRIPT);
        if (cleanup_script.is_open()) {
            cleanup_script << "select vdisk file=" << VHDX_FILE << endl;
            cleanup_script << "detach vdisk" << endl;
            cleanup_script << "exit" << endl;
            cleanup_script.close();
            system(detach_command.c_str());
        }
        if (std::remove(VHDX_FILE.c_str()) == 0) {
            wcout << L"* 既存のVHDXファイルを強制削除しました。" << endl;
        }
        else {
            wcout << L"* 既存のVHDXファイルはありませんでした (または削除失敗)。" << endl;
        }
    }

    // VHDX作成・マウントスクリプトの作成
    ofstream script_file(DISKPART_SCRIPT);
    script_file << "create vdisk file=" << VHDX_FILE << " maximum=512 type=fixed" << endl;
    script_file << "select vdisk file=" << VHDX_FILE << endl;
    script_file << "attach vdisk" << endl;

    // ? 修正: ディスクをGPT形式に初期化する（Diskpartエラー対策）
    script_file << "convert gpt" << endl;

    script_file << "create partition primary" << endl;

    // UEFI System Partition GUIDを設定
    script_file << "set id=c12a7328-f81f-11d2-ba4b-00a0c93ec93b" << endl;

    script_file << "format fs=fat32 quick label=\"UEFI_FAT32\"" << endl;
    script_file << "assign letter=" << DRIVE_LETTER << endl;
    script_file << "exit" << endl;
    script_file.close();

    // diskpartの実行とデバッグログ
    string command = "diskpart /s " + DISKPART_SCRIPT;

    wcout << L"\n* 実行する Diskpart スクリプト (" << DISKPART_SCRIPT.c_str() << L"):" << endl;
    wcout << L"------------------------------------------" << endl;
    ifstream check_script(DISKPART_SCRIPT);
    string script_line;
    while (getline(check_script, script_line)) {
        wcout << script_line.c_str() << endl;
    }
    check_script.close();
    wcout << L"------------------------------------------" << endl;

    wcout << L"* Diskpart 実行開始..." << endl;
    system(command.c_str());

    std::remove(DISKPART_SCRIPT.c_str());

    if (GetFileAttributesA((DRIVE_LETTER + ":\\").c_str()) != INVALID_FILE_ATTRIBUTES) {
        wcout << L"\n? " << DRIVE_LETTER.c_str() << L"ドライブにVHDXがFAT32でマウントされました。" << endl;
    }
    else {
        wcerr << L"\n? " << DRIVE_LETTER.c_str() << L"ドライブが見つかりません。**Diskpartのエラーメッセージ**を確認してください。" << endl;
    }
}


// --- 関数: try_copy (省略) ---
bool try_copy(const string& source_path, const string& destination_path, const string& display_name, bool is_mandatory = false) {
    bool success = true;
    wcout << L"\n* コピー試行: " << display_name.c_str() << L" -> " << destination_path.c_str() << endl;

    if (GetFileAttributesA(source_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wcerr << L"  - ? 失敗: ソースファイルが見つかりません。パス: " << source_path.c_str() << endl;
        success = false;
    }
    else {
        if (system(("copy /Y \"" + source_path + "\" \"" + destination_path + "\" > NUL").c_str()) != 0) {
            wcerr << L"  - ? 失敗: コピーコマンドがエラーを返しました。**コンソールのエラーメッセージを確認してください。**" << endl;
            success = false;
        }
        else {
            wcout << L"  - ? 成功。" << endl;
        }
    }

    if (is_mandatory && !success) {
        return false;
    }
    return success;
}


// --- 関数: process_and_copy_nsh (省略) ---
bool process_and_copy_nsh(const string& source_path, const string& target_path) {
    ifstream input_file(source_path);
    if (!input_file.is_open()) {
        wcerr << L"  - ? 致命的失敗: startup.nsh のソースファイルが見つかりません。パス: " << source_path.c_str() << endl;
        return false;
    }

    string content((istreambuf_iterator<char>(input_file)), istreambuf_iterator<char>());
    input_file.close();

    string random_uuid = generate_random_hex(32);

    string search_serial = "PLS_RANDOM_SERIAL";
    size_t pos_serial = content.find(search_serial);
    int serial_count = 0;

    while (pos_serial != string::npos) {
        string current_random_serial = generate_random_hex(12);

        content.replace(pos_serial, search_serial.length(), current_random_serial);

        pos_serial = content.find(search_serial, pos_serial + current_random_serial.length());
        serial_count++;
    }

    string search_uuid = "PLS_RANDOM_UUID";
    size_t pos_uuid = content.find(search_uuid);
    int uuid_count = 0;
    while (pos_uuid != string::npos) {
        content.replace(pos_uuid, search_uuid.length(), random_uuid);

        pos_uuid = content.find(search_uuid, pos_uuid + random_uuid.length());
        uuid_count++;
    }

    ofstream output_file(target_path);
    if (!output_file.is_open()) {
        wcerr << L"  - ? 致命的失敗: startup.nsh を VHDX (" << target_path.c_str() << L") に書き込めません。" << endl;
        return false;
    }

    output_file << content;
    output_file.close();

    wcout << L"  - ? 成功。";
    wcout << L" (SERIAL:" << serial_count << L"回/個別値, UUID:" << uuid_count << L"回/同一値) 置換と書き込みを完了。" << endl;
    return true;
}


// --- 関数: copy_files_to_vhdx (省略) ---
void copy_files_to_vhdx() {
    string target_root = DRIVE_LETTER + ":\\";
    bool copy_success = true;

    if (GetFileAttributesA(target_root.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wcerr << L"\n致命的エラー: " << DRIVE_LETTER.c_str() << L"ドライブが見つかりません。diskpartが失敗した可能性があります。" << endl;
        return;
    }

    wcout << L"\n--- 2. VHDXへのファイルコピーとC:\\への " << CLEANUP_EXE_FILENAME.c_str() << L" コピー ---" << endl;

    string remove_staging_cmd = "cmd /c rd /s /q \"" + STAGING_DIR + "\"";
    string create_staging_cmd = "cmd /c mkdir \"" + STAGING_DIR + "\"";
    system(remove_staging_cmd.c_str());
    system(create_staging_cmd.c_str());

    string zip_full_path = get_full_path(ZIP_FILENAME);
    string unzip_command = "powershell -command \"Expand-Archive -Path '" + zip_full_path + "' -DestinationPath '" + STAGING_DIR + "' -Force\"";

    wcout << L"* " << ZIP_FILENAME.c_str() << L" を一時フォルダ (" << STAGING_DIR.c_str() << L") に展開します..." << endl;
    if (system(unzip_command.c_str()) != 0) {
        wcerr << L"  - ? ZIPファイルの展開に失敗しました。処理を中断します。" << endl;
        system(remove_staging_cmd.c_str());
        return;
    }
    wcout << L"  - ? 展開完了。" << endl;

    string efi_dir = target_root + "EFI";
    string boot_dir = target_root + "EFI\\BOOT";

    CreateDirectoryA(efi_dir.c_str(), NULL);
    CreateDirectoryA(boot_dir.c_str(), NULL);

    wcout << L"\n* 必要なファイルを Y:\\ にコピーします..." << endl;

    copy_success &= try_copy(STAGING_DIR + "afuefix64.efi", target_root + "afuefix64.efi", "afuefix64.efi", false);
    copy_success &= try_copy(STAGING_DIR + "amideefix64.efi", target_root + "amideefix64.efi", "amideefix64.efi", false);
    copy_success &= try_copy(STAGING_DIR + "ChgLogo.efi", target_root + "ChgLogo.efi", "ChgLogo.efi", false);
    copy_success &= try_copy(STAGING_DIR + "Compress.efi", target_root + "Compress.efi", "Compress.efi", false);
    copy_success &= try_copy(STAGING_DIR + "flash2.efi", target_root + "flash2.efi", "flash2.efi", false);
    copy_success &= try_copy(STAGING_DIR + "imageM1U.ROM", target_root + "imageM1U.ROM", "imageM1U.ROM", false);
    copy_success &= try_copy(STAGING_DIR + "changes.txt", target_root + "changes.txt", "changes.txt", false);
    copy_success &= try_copy(STAGING_DIR + "logo.nsh", target_root + "logo.nsh", "logo.nsh", false);

    string shell_source_path = STAGING_DIR + ZIP_SHELL_PATH;
    string shell_target_path = boot_dir + "\\" + BOOT_FILE_NAME;
    wcout << L"\n* UEFI Shellファイル (" << BOOT_FILE_NAME.c_str() << L") を Y:\\EFI\\BOOT\\BOOTX64.EFI としてコピー (必須)..." << endl;
    copy_success &= try_copy(shell_source_path, shell_target_path, BOOT_FILE_NAME + " -> BOOTX64.EFI", true);

    string startup_nsh_staging_path = STAGING_DIR + "EFI\\BOOT\\" + STARTUP_NSH_FILENAME;
    string startup_nsh_target_path = boot_dir + "\\" + STARTUP_NSH_FILENAME;
    wcout << L"\n* startup.nsh を処理し、ランダムなシリアルとUUIDを埋め込み中..." << endl;
    if (process_and_copy_nsh(startup_nsh_staging_path, startup_nsh_target_path)) {
        copy_success &= true;
    }
    else {
        copy_success &= false;
    }

    wcout << L"\n* " << CLEANUP_EXE_FILENAME.c_str() << L" を C:\\ にコピー中..." << endl;
    copy_success &= try_copy(get_full_path(CLEANUP_EXE_FILENAME), "C:\\" + CLEANUP_EXE_FILENAME, CLEANUP_EXE_FILENAME + " -> C:\\" + CLEANUP_EXE_FILENAME, true);

    system(remove_staging_cmd.c_str());
    wcout << L"\n一時フォルダ (" << STAGING_DIR.c_str() << L") を削除しました。" << endl;

    if (copy_success) {
        wcout << L"\n全ファイルコピー完了。??" << endl;
    }
    else {
        wcerr << L"\n**?? ファイルコピー処理に失敗しました。上記のエラーを確認し、解決後に再度実行してください。**" << endl;
    }
}


// --- 関数: Windowsブートメニューにエントリーを登録し、デフォルトに設定する (致命的修正済み) ---
void register_boot_entry() {
    wcout << L"\n--- 3. Windowsブートマネージャーにエントリーを登録 (自動起動設定) ---" << endl;

    const string TEMP_GUID_FILE = "temp_guid.txt";
    string copy_command = "bcdedit /copy {current} /d \"" + BOOT_ENTRY_NAME + "\" > " + TEMP_GUID_FILE;
    wcout << L"* 新しいブートエントリーを作成中..." << endl;

    if (system(copy_command.c_str()) != 0) {
        wcerr << L"  - ? エントリー作成に失敗しました。管理者権限を確認してください。" << endl;
        return;
    }

    ifstream guid_file(TEMP_GUID_FILE);
    string line;
    string new_guid = "";

    while (getline(guid_file, line)) {
        size_t start = line.find('{');
        size_t end = line.find('}');
        if (start != string::npos && end != string::npos && end > start) {
            new_guid = line.substr(start, end - start + 1);
            break;
        }
    }
    guid_file.close();
    std::remove(TEMP_GUID_FILE.c_str());

    if (new_guid.empty()) {
        wcerr << L"  - ? 新しいブートエントリーのGUIDを取得できませんでした。" << endl;
        return;
    }

    wcout << L"  - ? 新しいエントリーのGUID: " << new_guid.c_str() << endl;

    string vhdx_ref = "vhd=[C:]" + VHDX_FILE;

    wcout << L"* VHDX設定を適用中..." << endl;

    system(("bcdedit /set " + new_guid + " device partition=C: > NUL").c_str());
    system(("bcdedit /set " + new_guid + " osdevice " + vhdx_ref + " > NUL").c_str());

    // ? ? 致命的な修正: path設定を削除する ? ?
    // この設定が原因でVMが未署名と判断し、ブートに失敗してWindowsにフォールバックしていました。
    // system(("bcdedit /set " + new_guid + " path \\EFI\\BOOT\\BOOTX64.EFI > NUL").c_str()); // 削除！

    wcout << L"* 新しいエントリーをデフォルト起動に設定中..." << endl;
    if (system(("bcdedit /default " + new_guid).c_str()) == 0) {
        wcout << L"  - ? **設定完了: 次回起動時は「UEFI BIOS Edit Shell」が自動で起動します。**" << endl;
    }
    else {
        wcerr << L"  - ? デフォルト設定の変更に失敗しました。手動で設定してください。" << endl;
    }

    wcout << L"\n**?? UEFI Shell起動後、Windowsに戻るには、Windowsが起動可能な場合は以下のコマンドを管理者として実行してください:**" << endl;
    wcout << L"bcdedit /default {current}" << endl;
}


int main() {
    // ... (前略)

    // 0. 古いエントリーとサービスを削除
    cleanup_old_boot_entries();

    // 1. VHDXの準備 (GPT変換/パーティションID設定含む)
    setup_vhdx_disk();

    // 2. ファイルのコピー (startup.nshの内容をここで置換)
    copy_files_to_vhdx();

    // 3. ブートエントリー登録と自動起動設定 (path削除済み)
    register_boot_entry();

    // --- 4. サービスへの登録 ---
    // ... (省略)

    wcout << L"\n? すべての処理が完了しました。PCを再起動してください。" << endl;

    system("pause");

    return 0;
}