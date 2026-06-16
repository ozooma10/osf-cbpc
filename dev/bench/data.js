window.BENCHMARK_DATA = {
  "lastUpdate": 1781629146947,
  "repoUrl": "https://github.com/ozooma10/osf-cbpc",
  "entries": {
    "OSF CBPC": [
      {
        "commit": {
          "author": {
            "email": "98544147+ozooma10@users.noreply.github.com",
            "name": "ozooma10",
            "username": "ozooma10"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "93dec5f3ea95b521cfd46ca3d252bc730a618ba2",
          "message": "Merge pull request #2 from ozooma10/fix/gh-pages-deploy-concurrency\n\nci(bench): serialize gh-pages publishing to fix wedged Pages deploys",
          "timestamp": "2026-06-16T12:58:11-04:00",
          "tree_id": "587728b40be0ff2c2514845bd372e80f3b9ff24a",
          "url": "https://github.com/ozooma10/osf-cbpc/commit/93dec5f3ea95b521cfd46ca3d252bc730a618ba2"
        },
        "date": 1781629145694,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "solver/player (5 bones, scatter)",
            "value": 50.4104,
            "range": "± 6.09",
            "unit": "ns/bone",
            "extra": "median 50.56 ns, mean 51.12 ns"
          },
          {
            "name": "solver/crowd (M2-scale)",
            "value": 50.2943,
            "range": "± 15.90",
            "unit": "ns/bone",
            "extra": "median 50.50 ns, mean 51.06 ns"
          },
          {
            "name": "registry/current   1-thread",
            "value": 40.4008,
            "range": "± 0.00",
            "unit": "ns/lookup",
            "extra": "median 40.40 ns, mean 40.40 ns"
          },
          {
            "name": "registry/snapshot  1-thread",
            "value": 1.8472,
            "range": "± 0.00",
            "unit": "ns/lookup",
            "extra": "median 1.85 ns, mean 1.85 ns"
          }
        ]
      }
    ]
  }
}