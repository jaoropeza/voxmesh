using System.Net;
using System.Net.Http.Json;
using Microsoft.AspNetCore.Mvc.Testing;

namespace VoxMesh.ControlPlane.Api.Tests;

public sealed class ApiEndpointTests : IClassFixture<WebApplicationFactory<Program>>
{
    private readonly WebApplicationFactory<Program> _factory;

    public ApiEndpointTests(WebApplicationFactory<Program> factory)
    {
        _factory = factory;
    }

    [Fact]
    public async Task HealthEndpoint_ReturnsHealthy()
    {
        using var client = _factory.CreateClient();

        var response = await client.GetAsync(new Uri("/healthz", UriKind.Relative));

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("Healthy", await response.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task VersionEndpoint_ReportsServiceAndSemanticVersion()
    {
        using var client = _factory.CreateClient();

        var payload = await client.GetFromJsonAsync<VersionPayload>(
            new Uri("/version", UriKind.Relative));

        Assert.NotNull(payload);
        Assert.Equal("control-plane", payload.Service);
        Assert.Matches(@"^\d+\.\d+\.\d+", payload.Version);
    }

    private sealed record VersionPayload(string Service, string Version);
}
