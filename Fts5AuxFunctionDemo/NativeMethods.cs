using System.Runtime.InteropServices;
using SQLitePCL;

namespace Fts5AuxFunctionDemo;

public static class NativeMethods
{
    [DllImport("matched_columns")]
    public static extern int fts5_register_matched_columns(sqlite3 db);
}