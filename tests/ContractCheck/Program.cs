using smart_locking_be.Infrastructure.Services;
var codec = new BluetoothProtocol();
string folder = args[1];
if (args[0] == "generate")
{
    Directory.CreateDirectory(folder);
    string device="LKR-TEST", phone="0901234567";
    Guid delivery=Guid.ParseExact(new string('1',32),"N"), returned=Guid.ParseExact(new string('2',32),"N");
    var expiry=DateTimeOffset.FromUnixTimeSeconds(1792443600);
    File.WriteAllLines(Path.Combine(folder,"commands.txt"), new[] {
        codec.CreateCommand(device,delivery,1,"D",phone,"-",expiry,Guid.ParseExact(new string('a',32),"N")),
        codec.CreateCommand(device,delivery,1,"P",phone,"-",expiry,Guid.ParseExact(new string('b',32),"N")),
        codec.CreateCommand(device,returned,1,"R",phone,"471839",expiry,Guid.ParseExact(new string('c',32),"N")),
        codec.CreateCommand(device,returned,1,"G",phone,"471839",expiry,Guid.ParseExact(new string('d',32),"N"))
    });
    Console.WriteLine("Generated D/P/R/G commands with actual BE codec.");
}
else
{
    var events=File.ReadAllLines(Path.Combine(folder,"events.txt"));
    if(events.Length!=4) throw new Exception("Expected four physical door-cycle events.");
    for(int i=0;i<events.Length;i++)
    {
        var item=codec.ParseOfflineEvent(events[i],"LKR-TEST");
        if(item.Kind!="DPRG"[i].ToString() || item.Sequence!=i+1 || item.ElapsedMs<50 || item.Phone!="0901234567")
            throw new Exception("Firmware event does not match BE contract.");
        if(codec.CreateOfflineAcknowledgment("LKR-TEST",item.EventId)!=$"k1|{item.EventId:N}")
            throw new Exception("ACK contract mismatch.");
    }
    var confirmation=codec.ParsePinProvisioned("q1|11111111111111111111111111111111|000001","LKR-TEST");
    if(confirmation.Pin!="000001") throw new Exception("Leading-zero PIN lost.");
    Console.WriteLine("PASS: actual BE codec accepted four firmware events and ACK/PIN contracts.");
}
