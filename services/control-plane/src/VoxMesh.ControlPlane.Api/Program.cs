using System.Reflection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddHealthChecks();

var app = builder.Build();

// Liveness/readiness for Kubernetes probes and CI smoke tests.
app.MapHealthChecks("/healthz");

app.MapGet("/version", () =>
{
    var assembly = Assembly.GetExecutingAssembly();
    var version = assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()
        ?.InformationalVersion ?? "unknown";
    return Results.Ok(new VersionInfo("control-plane", version));
});

app.Run();

internal sealed record VersionInfo(string Service, string Version);

// Exposes the entry point to WebApplicationFactory in integration tests.
public partial class Program;
