using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

// Small startup selector. Profile names and colors are data in profiles.json; none are compiled in.
public sealed class FamilyProfile
{
    public string DisplayName { get; set; }
    public string ClientId { get; set; }
    public string AccentColor { get; set; }
    public string ComputerName { get; set; }
}

public static class FamilyProfileStore
{
    public static string ValidateId(string value)
    {
        if (String.IsNullOrWhiteSpace(value) || !System.Text.RegularExpressions.Regex.IsMatch(value, "^(?:[A-Za-z0-9][A-Za-z0-9_-]{0,39}|profile-[0-9A-Fa-f]{32})$"))
            throw new InvalidDataException("Profile ID is invalid.");
        return value;
    }
    public static List<FamilyProfile> Read(string path)
    {
        var serializer = new JavaScriptSerializer();
        var items = serializer.Deserialize<List<FamilyProfile>>(File.ReadAllText(path, Encoding.UTF8));
        if (items == null) throw new InvalidDataException("Profile list is empty or invalid.");
        var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var p in items) {
            ValidateId(p.ClientId);
            if (!ids.Add(p.ClientId)) throw new InvalidDataException("Duplicate profile ID: " + p.ClientId);
            if (String.IsNullOrWhiteSpace(p.DisplayName) || p.DisplayName.Length > 40) throw new InvalidDataException("A profile name is missing or too long.");
            if (p.AccentColor == null || !System.Text.RegularExpressions.Regex.IsMatch(p.AccentColor, "^#[0-9A-Fa-f]{6}$")) throw new InvalidDataException("Invalid color for " + p.ClientId);
        }
        return items;
    }
    public static FamilyProfile Create(string path, string name, string color)
    {
        name = (name ?? "").Trim();
        if (name.Length == 0 || name.Length > 40 || name.Any(Char.IsControl)) throw new InvalidDataException("Enter a name from 1 to 40 characters.");
        if (!System.Text.RegularExpressions.Regex.IsMatch(color ?? "", "^#[0-9A-Fa-f]{6}$")) throw new InvalidDataException("Choose a valid color.");
        string lockPath = path + ".lock";
        using (var held = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None)) {
            var items = Read(path);
            if (items.Any(p => String.Equals(p.DisplayName, name, StringComparison.OrdinalIgnoreCase))) throw new InvalidDataException("That profile name already exists.");
            string id;
            do { id = "profile-" + Guid.NewGuid().ToString("N"); } while (items.Any(p => String.Equals(p.ClientId, id, StringComparison.OrdinalIgnoreCase)));
            var created = new FamilyProfile { DisplayName=name, ClientId=id, AccentColor=color.ToUpperInvariant(), ComputerName="" };
            items.Add(created);
            var json = new JavaScriptSerializer().Serialize(items);
            string temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
            try {
                File.WriteAllText(temp, json, new UTF8Encoding(false));
                if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path);
            } finally { if (File.Exists(temp)) File.Delete(temp); }
            return created;
        }
    }
}

public sealed class Picker : Form
{
    [StructLayout(LayoutKind.Sequential)] struct XINPUT_GAMEPAD { public ushort buttons; public byte lt, rt; public short lx, ly, rx, ry; }
    [StructLayout(LayoutKind.Sequential)] struct XINPUT_STATE { public uint packet; public XINPUT_GAMEPAD gamepad; }
    [DllImport("xinput1_4.dll", EntryPoint="XInputGetState", SetLastError=true)] static extern uint XInputGetState14(uint index, out XINPUT_STATE state);
    const ushort DPAD_UP=0x0001, DPAD_DOWN=0x0002, DPAD_LEFT=0x0004, DPAD_RIGHT=0x0008, A=0x1000, B=0x2000;
    readonly string profilesPath, resultPath;
    List<FamilyProfile> profiles;
    FlowLayoutPanel cards;
    int selected=0;
    Timer poll;
    uint lastButtons=0;
    public Picker(string profileFile, string resultFile) {
        profilesPath=profileFile; resultPath=resultFile;
        Text="Choose a family profile"; BackColor=Color.FromArgb(24,24,28); ForeColor=Color.White;
        Font=new Font("Segoe UI", 14, FontStyle.Regular); StartPosition=FormStartPosition.CenterScreen;
        MinimumSize=new Size(760,500); Size=new Size(1000,700); KeyPreview=true;
        try { profiles=FamilyProfileStore.Read(profilesPath); } catch(Exception ex) { MessageBox.Show(this,ex.Message,"Profiles unavailable",MessageBoxButtons.OK,MessageBoxIcon.Error); throw; }
        try { if(File.Exists(resultPath)) File.Delete(resultPath); } catch { }
        Build();
        KeyDown += OnKey;
        poll=new Timer { Interval=120 }; poll.Tick += PollController; poll.Start();
    }
    void Build() {
        var heading=new Label { Text="Who is playing?", Dock=DockStyle.Top, Height=72, TextAlign=ContentAlignment.MiddleCenter, Font=new Font("Segoe UI",24,FontStyle.Bold), ForeColor=Color.White };
        cards=new FlowLayoutPanel { Dock=DockStyle.Fill, AutoScroll=true, WrapContents=true, Padding=new Padding(30), BackColor=BackColor };
        Controls.Add(cards); Controls.Add(heading);
        foreach(var p in profiles) AddCard(p);
        var add=new Button { Text="＋  Create profile", Width=250, Height=96, Margin=new Padding(14), FlatStyle=FlatStyle.Flat, BackColor=Color.FromArgb(42,42,48), ForeColor=Color.White, Font=new Font("Segoe UI",17,FontStyle.Bold) };
        add.FlatAppearance.BorderColor=Color.Gainsboro; add.FlatAppearance.BorderSize=2; add.Click += (s,e)=>CreateProfile(); cards.Controls.Add(add);
        var footer=new Label { Text="← / → or controller D-pad   Enter / A: select   Esc / B: cancel", Dock=DockStyle.Bottom, Height=48, TextAlign=ContentAlignment.MiddleCenter, ForeColor=Color.Gainsboro, Font=new Font("Segoe UI",12) };
        Controls.Add(footer); footer.BringToFront();
        SelectCard(0);
    }
    void AddCard(FamilyProfile p) {
        var c=ColorTranslator.FromHtml(p.AccentColor);
        var b=new Button { Text=p.DisplayName, Tag=p, Width=250, Height=128, Margin=new Padding(14), FlatStyle=FlatStyle.Flat, BackColor=Color.FromArgb(42,42,48), ForeColor=Color.White, Font=new Font("Segoe UI",20,FontStyle.Bold) };
        b.FlatAppearance.BorderColor=c; b.FlatAppearance.BorderSize=3; b.Click += (s,e)=>SelectProfile((FamilyProfile)((Button)s).Tag); cards.Controls.Add(b);
    }
    void SelectCard(int index) { if(cards.Controls.Count==0)return; selected=(index+cards.Controls.Count)%cards.Controls.Count; for(int i=0;i<cards.Controls.Count;i++){ var b=(Button)cards.Controls[i]; b.FlatAppearance.BorderSize=i==selected?6:3; } cards.Controls[selected].Focus(); }
    void SelectProfile(FamilyProfile p) { try { FamilyProfileStore.ValidateId(p.ClientId); File.WriteAllText(resultPath,p.ClientId,new UTF8Encoding(false)); DialogResult=DialogResult.OK; Close(); } catch(Exception ex){MessageBox.Show(this,ex.Message,"Could not select profile",MessageBoxButtons.OK,MessageBoxIcon.Error);} }
    void CreateProfile() {
        poll.Stop();
        try {
        using(var form=new Form { Text="Create profile", StartPosition=FormStartPosition.CenterParent, BackColor=BackColor, ForeColor=ForeColor, Font=Font, FormBorderStyle=FormBorderStyle.FixedDialog, MinimizeBox=false, MaximizeBox=false, ClientSize=new Size(430,190) }) {
            var label=new Label { Text="Profile name", Left=18,Top=18,Width=390,Height=28 };
            var name=new TextBox { Left=18,Top=52,Width=390,MaxLength=40,Font=new Font("Segoe UI",15) };
            var color=Color.FromArgb(85,136,204);
            var choose=new Button { Text="Outline color…", Left=18,Top=100,Width=190,Height=42 };
            choose.Click+=(s,e)=>{using(var d=new ColorDialog { Color=color, FullOpen=true }) if(d.ShowDialog(form)==DialogResult.OK) color=d.Color;};
            var ok=new Button { Text="Create", Left=258,Top=100,Width=150,Height=42,DialogResult=DialogResult.OK };
            form.Controls.AddRange(new Control[]{label,name,choose,ok}); form.AcceptButton=ok;
            if(form.ShowDialog(this)!=DialogResult.OK)return;
            try { var p=FamilyProfileStore.Create(profilesPath,name.Text,String.Format("#{0:X2}{1:X2}{2:X2}",color.R,color.G,color.B)); profiles=FamilyProfileStore.Read(profilesPath); cards.Controls.Clear(); foreach(var item in profiles)AddCard(item); AddCreateButton(); SelectCard(profiles.Count-1); }
            catch(Exception ex){MessageBox.Show(this,ex.Message,"Profile not created",MessageBoxButtons.OK,MessageBoxIcon.Error);}
        }
        } finally { poll.Start(); }
    }
    void AddCreateButton(){var add=new Button{Text="＋  Create profile",Width=250,Height=96,Margin=new Padding(14),FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(42,42,48),ForeColor=Color.White,Font=new Font("Segoe UI",17,FontStyle.Bold)}; add.FlatAppearance.BorderColor=Color.Gainsboro;add.FlatAppearance.BorderSize=2;add.Click+=(s,e)=>CreateProfile();cards.Controls.Add(add);}
    void OnKey(object s,KeyEventArgs e){if(e.KeyCode==Keys.Left||e.KeyCode==Keys.Up){SelectCard(selected-1);e.Handled=true;}else if(e.KeyCode==Keys.Right||e.KeyCode==Keys.Down){SelectCard(selected+1);e.Handled=true;}else if(e.KeyCode==Keys.Enter){((Button)cards.Controls[selected]).PerformClick();e.Handled=true;}else if(e.KeyCode==Keys.Escape){DialogResult=DialogResult.Cancel;Close();}}
    void PollController(object s,EventArgs e){try{for(uint i=0;i<4;i++){XINPUT_STATE st;if(XInputGetState14(i,out st)!=0)continue;uint now=st.gamepad.buttons;uint pressed=now&~lastButtons;lastButtons=now;if((pressed&(DPAD_LEFT|DPAD_UP))!=0)SelectCard(selected-1);else if((pressed&(DPAD_RIGHT|DPAD_DOWN))!=0)SelectCard(selected+1);else if((pressed&A)!=0)((Button)cards.Controls[selected]).PerformClick();else if((pressed&B)!=0){DialogResult=DialogResult.Cancel;Close();}break;}}catch{}}
}
public static class Program
{
    [STAThread] public static int Main(string[] args){if(args.Length!=2){MessageBox.Show("Expected profile JSON and result file paths.","Family profile picker");return 2;}Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);try{using(var f=new Picker(Path.GetFullPath(args[0]),Path.GetFullPath(args[1])))Application.Run(f);return File.Exists(args[1])?0:1;}catch(Exception ex){MessageBox.Show(ex.Message,"Family profile picker",MessageBoxButtons.OK,MessageBoxIcon.Error);return 2;}}
}


